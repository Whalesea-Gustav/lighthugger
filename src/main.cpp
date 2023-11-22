#include "allocations/base.h"
#include "allocations/persistently_mapped.h"
#include "allocations/staging.h"
#include "debugging.h"
#include "descriptor_set.h"
#include "pch.h"
#include "pipelines.h"
#include "projection.h"
#include "resources/image_loading.h"
#include "resources/mesh_loading.h"
#include "sync.h"

const auto u64_max = std::numeric_limits<uint64_t>::max();

// Sources:
// https://vkguide.dev
// https://www.glfw.org/docs/latest/vulkan_guide.html
// https://vulkan-tutorial.com
// https://github.com/charles-lunarg/vk-bootstrap/blob/main/docs/getting_started.md
// https://github.com/KhronosGroup/Vulkan-Hpp
// https://lesleylai.info/en/vk-khr-dynamic-rendering/
// https://github.com/dokipen3d/vulkanHppMinimalExample/blob/master/main.cpp

#include "frame_resources.h"
#include "rendering.h"

struct CameraParams {
    glm::vec3 position;
    glm::vec3 velocity = glm::vec3(0.0);
    float fov;

    float yaw = 0.0;
    float pitch = 0.0;

    float sun_latitude;
    float sun_longitude;

    glm::vec3 facing() {
        return glm::vec3(
            cosf(yaw) * cosf(pitch),
            sinf(pitch),
            sinf(yaw) * cosf(pitch)
        );
    }

    glm::vec3 right() {
        return glm::vec3(-sinf(yaw), 0.0, cosf(yaw));
    }

    glm::vec3 sun_dir() {
        return glm::vec3(
            cosf(sun_latitude) * cosf(sun_longitude),
            sinf(sun_longitude),
            sinf(sun_latitude) * cosf(sun_longitude)
        );
    }

    void move_camera(glm::ivec3 movement_vector) {
        if (movement_vector == glm::ivec3(0)) {
            return;
        }

        auto movement = glm::normalize(glm::vec3(movement_vector)) * 2.0f;

        position += movement.x * right();
        position += movement.z * facing();
        position.y += movement.y;
    }
};

struct KeyboardState {
    bool left;
    bool right;
    bool up;
    bool down;
    bool w;
    bool a;
    bool s;
    bool d;
    bool shift;
    bool control;
    bool grab_toggled;
};

void glfw_key_callback(
    GLFWwindow* window,
    int key,
    int /*scancode*/,
    int action,
    int /*mods*/
) {
    KeyboardState& keyboard_state =
        *static_cast<KeyboardState*>(glfwGetWindowUserPointer(window));
    switch (key) {
        case GLFW_KEY_LEFT:
            keyboard_state.left = action != GLFW_RELEASE;
            break;
        case GLFW_KEY_RIGHT:
            keyboard_state.right = action != GLFW_RELEASE;
            break;
        case GLFW_KEY_UP:
            keyboard_state.up = action != GLFW_RELEASE;
            break;
        case GLFW_KEY_DOWN:
            keyboard_state.down = action != GLFW_RELEASE;
            break;
        case GLFW_KEY_W:
            keyboard_state.w = action != GLFW_RELEASE;
            break;
        case GLFW_KEY_A:
            keyboard_state.a = action != GLFW_RELEASE;
            break;
        case GLFW_KEY_S:
            keyboard_state.s = action != GLFW_RELEASE;
            break;
        case GLFW_KEY_D:
            keyboard_state.d = action != GLFW_RELEASE;
            break;
        case GLFW_KEY_LEFT_SHIFT:
            keyboard_state.shift = action != GLFW_RELEASE;
            break;
        case GLFW_KEY_LEFT_CONTROL:
            keyboard_state.control = action != GLFW_RELEASE;
            break;
        case GLFW_KEY_G:
            keyboard_state.grab_toggled ^= (action == GLFW_PRESS);
            if (keyboard_state.grab_toggled) {
                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            } else {
                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            }
            break;
    }
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, GLFW_TRUE);
    }
}

struct RaiiAllocator {
    vma::Allocator allocator;

    ~RaiiAllocator() {
        allocator.destroy();
    }
};

int main() {
    glfwInit();

    auto vulkan_version = VK_API_VERSION_1_2;

    vk::ApplicationInfo appInfo = {
        .pApplicationName = "Hello Triangle",
        .applicationVersion = VK_MAKE_API_VERSION(0, 1, 0, 0),
        .pEngineName = "No Engine",
        .engineVersion = VK_MAKE_API_VERSION(0, 1, 0, 0),
        .apiVersion = vulkan_version};

    auto glfwExtensionCount = 0u;
    auto glfwExtensions =
        glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
    std::vector<const char*> instance_extensions(
        glfwExtensions,
        glfwExtensions + glfwExtensionCount
    );
    std::vector<const char*> layers;

    auto debug_enabled = true;

    if (debug_enabled) {
        instance_extensions.push_back("VK_EXT_debug_utils");
        layers.push_back("VK_LAYER_KHRONOS_validation");
    }

    VULKAN_HPP_DEFAULT_DISPATCHER.init();

    vk::raii::Context context;

    vk::raii::Instance instance(
        context,
        vk::InstanceCreateInfo {
            .flags = {},
            .pApplicationInfo = &appInfo,
            .enabledLayerCount = static_cast<uint32_t>(layers.size()),
            .ppEnabledLayerNames = layers.data(),
            .enabledExtensionCount =
                static_cast<uint32_t>(instance_extensions.size()),
            .ppEnabledExtensionNames = instance_extensions.data()}
    );

    VULKAN_HPP_DEFAULT_DISPATCHER.init(*instance, vkGetInstanceProcAddr);

    std::optional<vk::raii::DebugUtilsMessengerEXT> messenger = std::nullopt;

    if (debug_enabled) {
        messenger = instance.createDebugUtilsMessengerEXT(
            {.flags = {},
             .messageSeverity = vk::DebugUtilsMessageSeverityFlagBitsEXT::eError
                 | vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning
                 | vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose
                 | vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo,
             .messageType = vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral
                 | vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation
                 | vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance,
             .pfnUserCallback = debug_message_callback},
            nullptr
        );
    }

    // todo: physical device and queue family selection.
    std::vector<vk::raii::PhysicalDevice> physicalDevices =
        instance.enumeratePhysicalDevices();
    auto phys_device = physicalDevices[0];
    auto queueFamilyProperties = phys_device.getQueueFamilyProperties();
    //auto queue_fam = queueFamilyProperties[0];

    uint32_t graphics_queue_family = 0;

    float queue_prio = 1.0f;

    vk::DeviceQueueCreateInfo device_queue_create_info = {
        .queueCount = 1,
        .pQueuePriorities = &queue_prio};

    auto vulkan_1_2_features = vk::PhysicalDeviceVulkan12Features {
        .drawIndirectCount = true,
        .descriptorBindingPartiallyBound = true,
        .runtimeDescriptorArray = true,
        .bufferDeviceAddress = true,
    };

    vk::PhysicalDeviceDynamicRenderingFeatures dyn_rendering_features = {
        .pNext = &vulkan_1_2_features,
        .dynamicRendering = true};

    auto vulkan_1_0_features = vk::PhysicalDeviceFeatures {
        .multiDrawIndirect = true,
        .shaderInt64 = true,
        .shaderInt16 = true,
    };

    auto device_extensions = std::array {
        "VK_KHR_swapchain",
        "VK_KHR_dynamic_rendering",
        "VK_KHR_shader_non_semantic_info"};

    vk::raii::Device device = phys_device.createDevice(
        {
            .pNext = &dyn_rendering_features,
            .queueCreateInfoCount = 1,
            .pQueueCreateInfos = &device_queue_create_info,
            .enabledLayerCount = 0,
            .ppEnabledLayerNames = nullptr,
            .enabledExtensionCount =
                static_cast<uint32_t>(device_extensions.size()),
            .ppEnabledExtensionNames = device_extensions.data(),
            .pEnabledFeatures = &vulkan_1_0_features,
        },
        nullptr
    );

    VULKAN_HPP_DEFAULT_DISPATCHER
        .init(*instance, vkGetInstanceProcAddr, *device);

    auto graphics_queue = device.getQueue(graphics_queue_family, 0);

    vk::Extent2D extent = {
        .width = 640,
        .height = 480,
    };

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    auto window = glfwCreateWindow(
        static_cast<int>(extent.width),
        static_cast<int>(extent.height),
        "Window Title",
        NULL,
        NULL
    );

    VkSurfaceKHR _surface;
    check_vk_result(static_cast<vk::Result>(
        glfwCreateWindowSurface(*instance, window, NULL, &_surface)
    ));
    vk::raii::SurfaceKHR surface = vk::raii::SurfaceKHR(instance, _surface);

    // Todo: get minimagecount and imageformat properly.
    vk::SwapchainCreateInfoKHR swapchain_create_info = {
        .surface = *surface,
        .minImageCount = 3,
        // Note: normally/ideally you should be using an srgb image format!
        // However, I'm (going to be) doing the final tonemapping in a compute shader where
        // I can't rely on the hardware linear->srgb transfer function.
        // Additionally, for whatever reason, imgui applies it's own linear->srgb
        // transfer function.
        .imageFormat = vk::Format::eB8G8R8A8Unorm,
        .imageColorSpace = vk::ColorSpaceKHR::eSrgbNonlinear,
        .imageExtent = extent,
        .imageArrayLayers = 1,
        .imageUsage = vk::ImageUsageFlagBits::eColorAttachment
            | vk::ImageUsageFlagBits::eStorage,
        .presentMode = vk::PresentModeKHR::eFifo,
    };
    auto swapchain = device.createSwapchainKHR(swapchain_create_info);

    auto swapchain_images = swapchain.getImages();
    for (size_t i = 0; i < swapchain_images.size(); i++) {
        VkImage c_image = swapchain_images[i];
        std::string name = std::string("swapchain image ") + std::to_string(i);
        device.setDebugUtilsObjectNameEXT(vk::DebugUtilsObjectNameInfoEXT {
            .objectType = vk::ObjectType::eImage,
            .objectHandle = reinterpret_cast<uint64_t>(c_image),
            .pObjectName = name.data()});
    }
    auto swapchain_image_views = create_swapchain_image_views(
        device,
        swapchain_images,
        swapchain_create_info.imageFormat
    );

    vk::raii::CommandPool command_pool = device.createCommandPool({
        .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
        .queueFamilyIndex = graphics_queue_family,
    });

    // AMD VMA allocator

    vma::AllocatorCreateInfo allocatorCreateInfo = {
        .flags = vma::AllocatorCreateFlagBits::eBufferDeviceAddress,
        .physicalDevice = *phys_device,
        .device = *device,
        .instance = *instance,
        .vulkanApiVersion = vulkan_version,
    };

    vma::Allocator allocator;
    check_vk_result(vma::createAllocator(&allocatorCreateInfo, &allocator));
    // Push a raii container for the allocator onto the stack,
    // just to make sure it's destructor gets called
    // after all allocated objects are destroyed.
    RaiiAllocator raii_allocator = {.allocator = allocator};

    auto command_buffers =
        device.allocateCommandBuffers(vk::CommandBufferAllocateInfo {
            .commandPool = *command_pool,
            .level = vk::CommandBufferLevel::ePrimary,
            .commandBufferCount = 1});
    auto command_buffer = std::move(command_buffers[0]);

    auto present_semaphore = device.createSemaphore({});
    auto render_semaphore = device.createSemaphore({});
    auto render_fence =
        device.createFence({.flags = vk::FenceCreateFlagBits::eSignaled});

    auto pipelines = Pipelines::compile_pipelines(device);

    auto pool_sizes = std::array {
        vk::DescriptorPoolSize {
            .type = vk::DescriptorType::eSampledImage,
            .descriptorCount = 1024},
        vk::DescriptorPoolSize {
            .type = vk::DescriptorType::eSampler,
            .descriptorCount = 1024},
        vk::DescriptorPoolSize {
            .type = vk::DescriptorType::eStorageBuffer,
            .descriptorCount = 1024}};

    auto descriptor_pool = device.createDescriptorPool(
        {.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,

         .maxSets = 128,
         .poolSizeCount = pool_sizes.size(),
         .pPoolSizes = pool_sizes.data()}
    );

    std::vector<vk::DescriptorSetLayout> descriptor_sets_to_create;
    descriptor_sets_to_create.reserve(swapchain_images.size() + 1);

    for (uint32_t i = 0; i < swapchain_images.size(); i++) {
        descriptor_sets_to_create.push_back(
            *pipelines.dsl.swapchain_storage_image
        );
    }
    descriptor_sets_to_create.push_back(*pipelines.dsl.everything);

    std::vector<vk::raii::DescriptorSet> descriptor_sets =
        device.allocateDescriptorSets(vk::DescriptorSetAllocateInfo {
            .descriptorPool = *descriptor_pool,
            .descriptorSetCount =
                static_cast<uint32_t>(descriptor_sets_to_create.size()),
            .pSetLayouts = descriptor_sets_to_create.data()});

    auto everything_set = std::move(descriptor_sets.back());
    descriptor_sets.pop_back();
    auto swapchain_image_sets = std::move(descriptor_sets);

    auto descriptor_set = DescriptorSet(
        std::move(everything_set),
        std::move(swapchain_image_sets)
    );

    std::vector<AllocatedBuffer> temp_buffers;

    command_buffer.begin(
        {.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit}
    );

    // Load all resources

    auto powerplant = load_obj(
        "powerplant/Powerplant.obj",
        allocator,
        device,
        command_buffer,
        graphics_queue_family,
        temp_buffers,
        descriptor_set
    );

    auto shadowmap = ImageWithView(
        {.imageType = vk::ImageType::e2D,
         .format = vk::Format::eD32Sfloat,
         .extent =
             vk::Extent3D {
                 .width = 1024,
                 .height = 1024,
                 .depth = 1,
             },
         .mipLevels = 1,
         .arrayLayers = 4,
         .usage = vk::ImageUsageFlagBits::eDepthStencilAttachment
             | vk::ImageUsageFlagBits::eSampled},
        allocator,
        device,
        "shadowmap",
        {
            .aspectMask = vk::ImageAspectFlagBits::eDepth,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 4,
        },
        vk::ImageViewType::e2DArray
    );

    auto create_shadow_view = [&](uint32_t base_layer) {
        return device.createImageView(
            {.image = shadowmap.image.image,
             .viewType = vk::ImageViewType::e2D,
             .format = vk::Format::eD32Sfloat,
             .subresourceRange =
                 {
                     .aspectMask = vk::ImageAspectFlagBits::eDepth,
                     .baseMipLevel = 0,
                     .levelCount = 1,
                     .baseArrayLayer = base_layer,
                     .layerCount = 1,
                 }}
        );
    };

    auto shadowmap_layer_views = std::array {
        create_shadow_view(0),
        create_shadow_view(1),
        create_shadow_view(2),
        create_shadow_view(3)};

    auto powerplant_info = powerplant.get_info(device);
    auto instances = std::array {
        Instance(
            glm::translate(glm::mat4(1), glm::vec3(0, 0, 0)),
            powerplant_info
        ),
        Instance(
            glm::translate(glm::mat4(1), glm::vec3(0, 0, 100)),
            powerplant_info
        ),
        Instance(
            glm::translate(
                glm::rotate(glm::mat4(1), 180.0f, glm::vec3(0, 1, 0)),
                glm::vec3(200, 0, 0)
            ),
            powerplant_info
        )};

    auto resources = Resources {
        .resizing = ResizingResources(device, allocator, extent),
        .uniform_buffer = PersistentlyMappedBuffer(AllocatedBuffer(
            vk::BufferCreateInfo {
                .size = sizeof(Uniforms),
                .usage = vk::BufferUsageFlagBits::eUniformBuffer},
            {
                .flags = vma::AllocationCreateFlagBits::eMapped
                    | vma::AllocationCreateFlagBits::eHostAccessSequentialWrite,
                .usage = vma::MemoryUsage::eAuto,
            },
            allocator,
            "uniform_buffer"
        )),
        .shadowmap = std::move(shadowmap),
        .depth_info_buffer = AllocatedBuffer(
            vk::BufferCreateInfo {
                .size = sizeof(DepthInfoBuffer),
                .usage = vk::BufferUsageFlagBits::eStorageBuffer
                    | vk::BufferUsageFlagBits::eTransferDst},
            {
                .usage = vma::MemoryUsage::eAuto,
            },
            allocator,
            "depth_info_buffer"
        ),
        .instance_buffer = upload_via_staging_buffer(
            instances.data(),
            instances.size() * sizeof(Instance),
            allocator,
            vk::BufferUsageFlagBits::eStorageBuffer,
            "instance buffer",
            command_buffer,
            temp_buffers
        ),
        .draw_calls_buffer = AllocatedBuffer(
            vk::BufferCreateInfo {
                .size = sizeof(vk::DrawIndirectCommand) * 1024,
                .usage = vk::BufferUsageFlagBits::eIndirectBuffer
                    | vk::BufferUsageFlagBits::eStorageBuffer},
            {
                .usage = vma::MemoryUsage::eAuto,
            },
            allocator,
            "draw_calls_buffer"
        ),
        .draw_counts_buffer = AllocatedBuffer(
            vk::BufferCreateInfo {
                .size = sizeof(uint32_t),
                .usage = vk::BufferUsageFlagBits::eIndirectBuffer
                    | vk::BufferUsageFlagBits::eStorageBuffer
                    | vk::BufferUsageFlagBits::eTransferDst},
            {
                .usage = vma::MemoryUsage::eAuto,
            },
            allocator,
            "draw_counts_buffer"
        ),
        .max_num_draws = 1024,
        .shadowmap_layer_views = std::move(shadowmap_layer_views),
        .display_transform_lut = load_dds(
            "external/tony-mc-mapface/shader/tony_mc_mapface.dds",
            allocator,
            device,
            command_buffer,
            graphics_queue_family,
            temp_buffers
        ),
        .repeat_sampler = device.createSampler(vk::SamplerCreateInfo {
            .magFilter = vk::Filter::eLinear,
            .minFilter = vk::Filter::eLinear,
            .addressModeU = vk::SamplerAddressMode::eRepeat,
            .addressModeV = vk::SamplerAddressMode::eRepeat,
            .addressModeW = vk::SamplerAddressMode::eRepeat,
            .maxLod = VK_LOD_CLAMP_NONE}),
        .clamp_sampler = device.createSampler(vk::SamplerCreateInfo {
            .magFilter = vk::Filter::eLinear,
            .minFilter = vk::Filter::eLinear,
            .addressModeU = vk::SamplerAddressMode::eClampToEdge,
            .addressModeV = vk::SamplerAddressMode::eClampToEdge,
            .addressModeW = vk::SamplerAddressMode::eClampToEdge,
            .maxLod = VK_LOD_CLAMP_NONE}),
        .shadowmap_comparison_sampler =
            device.createSampler(vk::SamplerCreateInfo {
                .magFilter = vk::Filter::eLinear,
                .minFilter = vk::Filter::eLinear,
                .addressModeU = vk::SamplerAddressMode::eClampToEdge,
                .addressModeV = vk::SamplerAddressMode::eClampToEdge,
                .addressModeW = vk::SamplerAddressMode::eClampToEdge,
                .compareEnable = true,
                .compareOp = vk::CompareOp::eLess,
                .maxLod = VK_LOD_CLAMP_NONE})};

    command_buffer.end();

    {
        vk::PipelineStageFlags dst_stage_mask =
            vk::PipelineStageFlagBits::eTransfer;

        vk::SubmitInfo submit_info = {
            .pWaitDstStageMask = &dst_stage_mask,
            .commandBufferCount = 1,
            .pCommandBuffers = &*command_buffer,
        };
        auto init_fence = device.createFence({});
        graphics_queue.submit(submit_info, *init_fence);

        check_vk_result(device.waitForFences({*init_fence}, true, u64_max));

        // Drop temp buffers.
        temp_buffers.clear();
    }

    // Write initial descriptor sets.
    descriptor_set.write_descriptors(resources, device, swapchain_image_views);

    auto camera_params = CameraParams {
        .position = glm::vec3(-86.5, 15.5, -17.0),
        .fov = 45.0f,
        .sun_latitude = -2.555f,
        .sun_longitude = 0.3f,
    };

    auto tracy_ctx =
        TracyVkContext(*phys_device, *device, *graphics_queue, *command_buffer);

    ImGui::CreateContext();
    ImGui_ImplGlfw_InitForVulkan(window, true);
    auto imgui_init_info = ImGui_ImplVulkan_InitInfo {
        .Instance = *instance,
        .PhysicalDevice = *phys_device,
        .Device = *device,
        .QueueFamily = graphics_queue_family,
        .Queue = *graphics_queue,
        .PipelineCache = nullptr,
        .DescriptorPool = *descriptor_pool,
        .Subpass = 0,
        .MinImageCount = static_cast<uint32_t>(swapchain_images.size()),
        .ImageCount = static_cast<uint32_t>(swapchain_images.size()),
        .MSAASamples = VK_SAMPLE_COUNT_1_BIT,
        .UseDynamicRendering = true,
        .ColorAttachmentFormat = VkFormat(swapchain_create_info.imageFormat),
        .Allocator = nullptr,
        .CheckVkResultFn = nullptr,
    };
    assert(ImGui_ImplVulkan_Init(&imgui_init_info, nullptr));

    ImGui_ImplVulkan_CreateFontsTexture();

    KeyboardState keyboard_state = {};

    glfwSetWindowUserPointer(window, &keyboard_state);
    glfwSetKeyCallback(window, glfw_key_callback);

    auto prev_mouse = glm::dvec2(0.0, 0.0);
    glfwGetCursorPos(window, &prev_mouse.x, &prev_mouse.y);

    Uniforms* uniforms =
        reinterpret_cast<Uniforms*>(resources.uniform_buffer.mapped_ptr);
    uniforms->num_instances = instances.size();
    uniforms->sun_intensity = glm::vec3(1.0);

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        int current_width, current_height;
        glfwGetWindowSize(window, &current_width, &current_height);
        vk::Extent2D current_extent = {
            .width = static_cast<uint32_t>(current_width),
            .height = static_cast<uint32_t>(current_height)};
        if (extent != current_extent) {
            graphics_queue.waitIdle();

            extent = current_extent;

            swapchain_create_info.imageExtent = extent;
            swapchain_create_info.oldSwapchain = *swapchain;

            // todo: this prints a validation error on resize. Still works fine though.
            // ideally https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VK_EXT_swapchain_maintenance1.html is used to avoid this.
            swapchain = device.createSwapchainKHR(swapchain_create_info);

            swapchain_images = swapchain.getImages();
            swapchain_image_views = create_swapchain_image_views(
                device,
                swapchain_images,
                swapchain_create_info.imageFormat
            );
            for (size_t i = 0; i < swapchain_images.size(); i++) {
                VkImage c_image = swapchain_images[i];
                std::string name =
                    std::string("swapchain image ") + std::to_string(i);
                device.setDebugUtilsObjectNameEXT(
                    vk::DebugUtilsObjectNameInfoEXT {
                        .objectType = vk::ObjectType::eImage,
                        .objectHandle = reinterpret_cast<uint64_t>(c_image),
                        .pObjectName = name.data()}
                );
            }

            resources.resizing = ResizingResources(device, allocator, extent);
            descriptor_set.write_resizing_descriptors(
                resources.resizing,
                device,
                swapchain_image_views
            );
        }
        {
            int sun_left_right =
                int(keyboard_state.right) - int(keyboard_state.left);
            int sun_up_down = int(keyboard_state.up) - int(keyboard_state.down);

            camera_params.move_camera(glm::ivec3(
                int(keyboard_state.d) - int(keyboard_state.a),
                int(keyboard_state.shift) - int(keyboard_state.control),
                int(keyboard_state.w) - int(keyboard_state.s)
            ));

            camera_params.sun_latitude +=
                static_cast<float>(sun_left_right) * 0.025f;
            camera_params.sun_longitude +=
                static_cast<float>(sun_up_down) * 0.025f;
            camera_params.sun_longitude = std::clamp(
                camera_params.sun_longitude,
                0.0f,
                std::numbers::pi_v<float> / 2.0f
            );

            auto mouse = glm::dvec2(0.0, 0.0);
            glfwGetCursorPos(window, &mouse.x, &mouse.y);
            auto mouse_delta = mouse - prev_mouse;
            prev_mouse = mouse;

            if (keyboard_state.grab_toggled) {
                camera_params.pitch -=
                    static_cast<float>(mouse_delta.y) / 1024.0f;
                camera_params.yaw +=
                    static_cast<float>(mouse_delta.x) / 1024.0f;
                camera_params.pitch = std::clamp(
                    camera_params.pitch,
                    -std::numbers::pi_v<float> / 2.0f + 0.0001f,
                    std::numbers::pi_v<float> / 2.0f
                );
            }
        }

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        {
            ImGui::Checkbox("debug cascades", &uniforms->debug_cascades);
            ImGui::Checkbox("debug shadowmaps", &uniforms->debug_shadowmaps);
            ImGui::SliderFloat("fov", &camera_params.fov, 0.0f, 90.0f);
            ImGui::SliderFloat(
                "sun_intensity",
                &uniforms->sun_intensity.x,
                0.0f,
                100.0f
            );
            ImGui::Text(
                "camera pos: (%f, %f, %f)",
                camera_params.position.x,
                camera_params.position.y,
                camera_params.position.z
            );
            ImGui::Text("yaw: %f", camera_params.yaw);
            ImGui::Text("pitch: %f", camera_params.pitch);
            ImGui::Text("sun_latitude: %f", camera_params.sun_latitude);
            ImGui::Text("sun_longitude: %f", camera_params.sun_longitude);
            ImGui::Text("grab_toggled: %u", keyboard_state.grab_toggled);
            ImGui::Text(
                "powerplant bounding sphere rad: %f",
                powerplant.bounding_sphere_radius
            );
        }
        ImGui::Render();

        // Wait on the render fence to be signaled
        // (it's signaled before this loop starts so that we don't just block forever on the first frame)
        check_vk_result(device.waitForFences({*render_fence}, true, u64_max));
        device.resetFences({*render_fence});

        // Important! This needs to happen after waiting on the fence because otherwise we get race conditions
        // due to writing to a value that the previous frame is reading from.
        {
            auto view = glm::lookAt(
                camera_params.position,
                camera_params.position + camera_params.facing(),
                glm::vec3(0, 1, 0)
            );

            auto perspective = infinite_reverse_z_perspective(
                glm::radians(camera_params.fov),
                float(extent.width),
                float(extent.height),
                0.01f
            );

            uniforms->sun_dir = camera_params.sun_dir();
            uniforms->view = view;
            uniforms->combined_perspective_view = perspective * view;
            uniforms->inv_perspective_view = glm::inverse(perspective * view);
        }

        // Reset the command pool instead of resetting the single command buffer as
        // it's cheaper (afaik). Obviously don't do this if multiple command buffers are used.
        command_pool.reset();

        // Acquire the next swapchain image (waiting on the gpu-side and signaling the present semaphore when finished).
        auto [acquire_err, swapchain_image_index] =
            swapchain.acquireNextImage(u64_max, *present_semaphore);

        // This wraps vkBeginCommandBuffer.
        command_buffer.begin(
            {.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit}
        );

        render(
            command_buffer,
            pipelines,
            descriptor_set,
            resources,
            swapchain_images[swapchain_image_index],
            swapchain_image_views[swapchain_image_index],
            extent,
            graphics_queue_family,
            tracy_ctx,
            swapchain_image_index
        );
        TracyVkCollect(tracy_ctx, *command_buffer);

        command_buffer.end();

        vk::PipelineStageFlags dst_stage_mask =
            vk::PipelineStageFlagBits::eTransfer;
        // Submit the command buffer, waiting on the present semaphore and
        // signaling the render semaphore when the commands are finished.
        vk::SubmitInfo submit_info = {
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &*present_semaphore,
            .pWaitDstStageMask = &dst_stage_mask,
            .commandBufferCount = 1,
            .pCommandBuffers = &*command_buffer,
            .signalSemaphoreCount = 1,
            .pSignalSemaphores = &*render_semaphore,
        };
        // This wraps vkQueueSubmit.
        graphics_queue.submit(submit_info, *render_fence);

        // Present the swapchain image after having wated on the render semaphore.
        // This wraps vkQueuePresentKHR.
        check_vk_result(graphics_queue.presentKHR({
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &*render_semaphore,
            .swapchainCount = 1,
            .pSwapchains = &*swapchain,
            .pImageIndices = &swapchain_image_index,
        }));

        FrameMark;
    }

    // Wait until the device is idle so that we don't get destructor warnings about currently in-use resources.
    device.waitIdle();

    ImGui_ImplVulkan_Shutdown();
    TracyVkDestroy(tracy_ctx);

    return 0;
}
