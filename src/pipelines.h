#include <fstream>

std::vector<uint8_t> read_file_to_bytes(const char* filepath) {
    std::ifstream file_stream(filepath, std::ios::binary);

    assert(file_stream);

    std::vector<uint8_t> contents((std::istreambuf_iterator<char>(file_stream)), {});

    assert(!contents.empty());

    return contents;
}

vk::raii::ShaderModule create_shader_from_file(const vk::raii::Device& device, const char* filepath) {
    auto bytes = read_file_to_bytes(filepath);

    auto shader = device.createShaderModule(vk::ShaderModuleCreateInfo{
        .codeSize = bytes.size(),
        .pCode = (uint32_t*)bytes.data(),
    });

    return shader;
}

struct Pipelines {
    vk::raii::Pipeline display_transform;

    static std::vector<vk::raii::Pipeline> compile_pipelines(const vk::raii::Device& device, vk::Format swapchain_format) {
        auto blit_vs = create_shader_from_file(device, "compiled_shaders/blit_vs.spv");
        auto blit_ps = create_shader_from_file(device, "compiled_shaders/blit_ps.spv");

        auto pipeline_layout = device.createPipelineLayout({

        });

        std::array<vk::PipelineShaderStageCreateInfo, 2> blit_stages = {
            vk::PipelineShaderStageCreateInfo {
                .stage = vk::ShaderStageFlagBits::eVertex,
                .module = *blit_vs,
                .pName = "VSMain",
            },
            vk::PipelineShaderStageCreateInfo {
                .stage = vk::ShaderStageFlagBits::eFragment,
                .module = *blit_ps,
                .pName = "PSMain"
            }
        };

        auto multisample_state = vk::PipelineMultisampleStateCreateInfo {
            .rasterizationSamples = vk::SampleCountFlagBits::e1,
            .sampleShadingEnable = false,
            .minSampleShading = 1.0f,
            .alphaToCoverageEnable = false,
            .alphaToOneEnable = false,
        };

        auto vertex_input_state = vk::PipelineVertexInputStateCreateInfo {

        };

        auto viewport_state = vk::PipelineViewportStateCreateInfo {
            .viewportCount = 1,
            .scissorCount = 1,
        };

        auto rasterization_state = vk::PipelineRasterizationStateCreateInfo {
            .polygonMode = vk::PolygonMode::eFill,
            .cullMode = vk::CullModeFlagBits::eNone,
            .lineWidth = 1.0f
        };

        auto input_assembly_state = vk::PipelineInputAssemblyStateCreateInfo {
            .topology = vk::PrimitiveTopology::eTriangleList
        };

        std::array<vk::PipelineColorBlendAttachmentState, 1> color_blend_attachments = {vk::PipelineColorBlendAttachmentState {
            .blendEnable = false,
            .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA
        }};

        auto color_blend_state = vk::PipelineColorBlendStateCreateInfo {
            .logicOpEnable = false,
            .attachmentCount = color_blend_attachments.size(),
            .pAttachments = color_blend_attachments.data()
        };

        std::array<vk::DynamicState, 2> dynamic_states = {
            vk::DynamicState::eViewport,
            vk::DynamicState::eScissor,
        };

        auto dynamic_state = vk::PipelineDynamicStateCreateInfo {
            .dynamicStateCount = dynamic_states.size(),
            .pDynamicStates = dynamic_states.data()
        };

        std::array<vk::Format, 1> color_attachment_formats = {
            swapchain_format
        };

        auto pipeline_rendering_info = vk::PipelineRenderingCreateInfoKHR {
            .colorAttachmentCount = color_attachment_formats.size(),
            .pColorAttachmentFormats = color_attachment_formats.data()
        };

        auto pipeline_info = vk::GraphicsPipelineCreateInfo {
            .pNext = &pipeline_rendering_info,
            .stageCount = blit_stages.size(),
            .pStages = blit_stages.data(),
            .pVertexInputState = &vertex_input_state,
            .pInputAssemblyState = &input_assembly_state,
            .pViewportState = &viewport_state,
            .pRasterizationState = &rasterization_state,
            .pMultisampleState = &multisample_state,
            .pColorBlendState = &color_blend_state,
            .pDynamicState = &dynamic_state,
            .layout = *pipeline_layout,
        };

        std::array<vk::GraphicsPipelineCreateInfo, 1> pipeline_infos = {
            pipeline_info
        };

        return device.createGraphicsPipelines(nullptr, pipeline_infos);
    }

    Pipelines(std::vector<vk::raii::Pipeline> pipelines): display_transform(std::move(pipelines[0])) {}
    Pipelines(const vk::raii::Device& device, vk::Format swapchain_format): Pipelines(compile_pipelines(device, swapchain_format)) {}
};
