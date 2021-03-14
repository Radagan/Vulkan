/*
* Vulkan Example - Retrieving pipeline statistics
*
* Copyright (C) 2017-2021 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#include "vulkanexamplebase.h"
#include "VulkanglTFModel.h"

#define ENABLE_VALIDATION false

class VulkanExample : public VulkanExampleBase
{
public:
	struct Models {
		std::vector<vkglTF::Model> objects;
		int32_t objectIndex = 3;
		std::vector<std::string> names;
	} models;

	struct UniformData {
		glm::mat4 projection;
		glm::mat4 modelview;
		glm::vec4 lightPos = glm::vec4(-10.0f, -10.0f, 10.0f, 1.0f);
	};
	
	struct FrameObjects : public VulkanFrameObjects {
		vks::Buffer uniformBuffer;
		VkDescriptorSet descriptorSet;
	};
	std::vector<FrameObjects> frameObjects;

	int32_t gridSize = 3;
	int32_t cullMode = VK_CULL_MODE_BACK_BIT;
	bool blending = false;
	bool discard = false;
	bool wireframe = false;
	bool tessellation = false;

	VkPipeline pipeline = VK_NULL_HANDLE;
	VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
	VkDescriptorSetLayout descriptorSetLayout;

	// Vector for storing pipeline statistics results
	std::vector<uint64_t> pipelineStats;
	std::vector<std::string> pipelineStatNames;
	VkQueryPool queryPool;

	VulkanExample() : VulkanExampleBase(ENABLE_VALIDATION)
	{
		title = "Pipeline statistics";
		camera.setType(Camera::CameraType::firstperson);
		camera.setPosition(glm::vec3(-3.0f, 1.0f, -2.75f));
		camera.setRotation(glm::vec3(-15.25f, -46.5f, 0.0f));
		camera.setPerspective(60.0f, (float)width / (float)height, 0.1f, 256.0f);
		camera.setMovementSpeed(4.0f);
		camera.setRotationSpeed(0.25f);
		settings.overlay = true;
	}

	~VulkanExample()
	{
		if (device) {
			vkDestroyPipeline(device, pipeline, nullptr);
			vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
			vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
			vkDestroyQueryPool(device, queryPool, nullptr);
			for (FrameObjects& frame : frameObjects) {
				frame.uniformBuffer.destroy();
				destroyBaseFrameObjects(frame);
			}
		}
	}

	virtual void getEnabledFeatures()
	{
		// Support for pipeline statistics is optional
		if (deviceFeatures.pipelineStatisticsQuery) {
			enabledFeatures.pipelineStatisticsQuery = VK_TRUE;
		}
		else {
			vks::tools::exitFatal("Selected GPU does not support pipeline statistics!", VK_ERROR_FEATURE_NOT_PRESENT);
		}
		if (deviceFeatures.fillModeNonSolid) {
			enabledFeatures.fillModeNonSolid = VK_TRUE;
		}
		if (deviceFeatures.tessellationShader) {
			enabledFeatures.tessellationShader = VK_TRUE;
		}
	}

	// Create a query pool for storing the pipeline statistics we want to display
	void createQueryPool()
	{
		pipelineStatNames = {
			"Input assembly vertex count        ",
			"Input assembly primitives count    ",
			"Vertex shader invocations          ",
			"Clipping stage primitives processed",
			"Clipping stage primitives output    ",
			"Fragment shader invocations        "
		};
		if (deviceFeatures.tessellationShader) {
			pipelineStatNames.push_back("Tess. control shader patches       ");
			pipelineStatNames.push_back("Tess. eval. shader invocations     ");
		}
		pipelineStats.resize(pipelineStatNames.size());

		VkQueryPoolCreateInfo queryPoolInfo = {};
		queryPoolInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
		// This query pool will store pipeline statistics
		queryPoolInfo.queryType = VK_QUERY_TYPE_PIPELINE_STATISTICS;
		// Pipeline counters to be returned for this pool
		queryPoolInfo.pipelineStatistics =
			VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_VERTICES_BIT |
			VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_PRIMITIVES_BIT |
			VK_QUERY_PIPELINE_STATISTIC_VERTEX_SHADER_INVOCATIONS_BIT |
			VK_QUERY_PIPELINE_STATISTIC_CLIPPING_INVOCATIONS_BIT |
			VK_QUERY_PIPELINE_STATISTIC_CLIPPING_PRIMITIVES_BIT |
			VK_QUERY_PIPELINE_STATISTIC_FRAGMENT_SHADER_INVOCATIONS_BIT;
		if (deviceFeatures.tessellationShader) {
			queryPoolInfo.pipelineStatistics |=
				VK_QUERY_PIPELINE_STATISTIC_TESSELLATION_CONTROL_SHADER_PATCHES_BIT |
				VK_QUERY_PIPELINE_STATISTIC_TESSELLATION_EVALUATION_SHADER_INVOCATIONS_BIT;
		}
		queryPoolInfo.queryCount = deviceFeatures.tessellationShader ? 8 : 6;
		VK_CHECK_RESULT(vkCreateQueryPool(device, &queryPoolInfo, NULL, &queryPool));
	}

	void loadAssets()
	{
		// Objects
		std::vector<std::string> filenames = { "sphere.gltf", "teapot.gltf", "torusknot.gltf", "venus.gltf" };
		models.names = { "Sphere", "Teapot", "Torusknot", "Venus" };
		models.objects.resize(filenames.size());
		for (size_t i = 0; i < filenames.size(); i++) {
			models.objects[i].loadFromFile(getAssetPath() + "models/" + filenames[i], vulkanDevice, queue, vkglTF::FileLoadingFlags::PreTransformVertices | vkglTF::FileLoadingFlags::FlipY);
		}
	}

	void createDescriptors()
	{
		// Pool
		std::vector<VkDescriptorPoolSize> poolSizes = {
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3 * getFrameCount())
		};
		VkDescriptorPoolCreateInfo descriptorPoolInfo = vks::initializers::descriptorPoolCreateInfo(poolSizes, 3 * getFrameCount());
		VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));

		// Layout
		std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 0)
		};
		VkDescriptorSetLayoutCreateInfo descriptorLayout = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBindings);
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayout, nullptr, &descriptorSetLayout));

		// Sets
		for (FrameObjects& frame : frameObjects) {
			VkDescriptorSetAllocateInfo allocInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayout, 1);
			VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &frame.descriptorSet));
			std::vector<VkWriteDescriptorSet> writeDescriptorSets = {
				vks::initializers::writeDescriptorSet(frame.descriptorSet, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &frame.uniformBuffer.descriptor)
			};
			vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
		}
	}

	void createPipelines()
	{
		// Layout with push constant for passing object positions
		if (pipelineLayout == VK_NULL_HANDLE) {
			VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = vks::initializers::pipelineLayoutCreateInfo(&descriptorSetLayout, 1);
			VkPushConstantRange pushConstantRange = vks::initializers::pushConstantRange(VK_SHADER_STAGE_VERTEX_BIT, sizeof(glm::vec3), 0);
			pipelineLayoutCreateInfo.pushConstantRangeCount = 1;
			pipelineLayoutCreateInfo.pPushConstantRanges = &pushConstantRange;
			VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, nullptr, &pipelineLayout));
		}

		// Pipeline
		if (pipeline != VK_NULL_HANDLE) {
			// If the pipeline needs to be recreated, we need to wait for all command buffers to finish first
			vkQueueWaitIdle(queue);
			vkDestroyPipeline(device, pipeline, nullptr);
		}

		VkPipelineInputAssemblyStateCreateInfo inputAssemblyState = vks::initializers::pipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);
		VkPipelineRasterizationStateCreateInfo rasterizationState = vks::initializers::pipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, cullMode, VK_FRONT_FACE_COUNTER_CLOCKWISE, 0);
		VkPipelineColorBlendAttachmentState blendAttachmentState = vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE);		
		VkPipelineColorBlendStateCreateInfo colorBlendState = vks::initializers::pipelineColorBlendStateCreateInfo(1, &blendAttachmentState);
		VkPipelineDepthStencilStateCreateInfo depthStencilState = vks::initializers::pipelineDepthStencilStateCreateInfo(VK_TRUE,	VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL);
		VkPipelineViewportStateCreateInfo viewportState = vks::initializers::pipelineViewportStateCreateInfo(1, 1, 0);
		VkPipelineMultisampleStateCreateInfo multisampleState = vks::initializers::pipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT, 0);
		std::vector<VkDynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
		VkPipelineDynamicStateCreateInfo dynamicState = vks::initializers::pipelineDynamicStateCreateInfo(dynamicStateEnables.data(), static_cast<uint32_t>(dynamicStateEnables.size()), 0);
		VkPipelineTessellationStateCreateInfo tessellationState = vks::initializers::pipelineTessellationStateCreateInfo(3);

		VkGraphicsPipelineCreateInfo pipelineCI = vks::initializers::pipelineCreateInfo(pipelineLayout, renderPass, 0);
		pipelineCI.pInputAssemblyState = &inputAssemblyState;
		pipelineCI.pRasterizationState = &rasterizationState;
		pipelineCI.pColorBlendState = &colorBlendState;
		pipelineCI.pMultisampleState = &multisampleState;
		pipelineCI.pViewportState = &viewportState;
		pipelineCI.pDepthStencilState = &depthStencilState;
		pipelineCI.pDynamicState = &dynamicState;
		pipelineCI.pVertexInputState = vkglTF::Vertex::getPipelineVertexInputState({ vkglTF::VertexComponent::Position, vkglTF::VertexComponent::Normal, vkglTF::VertexComponent::Color });

		if (blending) {
			blendAttachmentState.blendEnable = VK_TRUE;
			blendAttachmentState.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
			blendAttachmentState.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
			blendAttachmentState.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
			blendAttachmentState.colorBlendOp = VK_BLEND_OP_ADD;
			blendAttachmentState.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
			blendAttachmentState.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
			blendAttachmentState.alphaBlendOp = VK_BLEND_OP_ADD;
			depthStencilState.depthWriteEnable = VK_FALSE;
		}

		if (discard) {
			rasterizationState.rasterizerDiscardEnable = VK_TRUE;
		}

		if (wireframe) {
			rasterizationState.polygonMode = VK_POLYGON_MODE_LINE;
		}

		std::vector<VkPipelineShaderStageCreateInfo> shaderStages;
		shaderStages.resize(tessellation ? 4 : 2);
		shaderStages[0] = loadShader(getShadersPath() + "pipelinestatistics/scene.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShadersPath() + "pipelinestatistics/scene.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

		if (tessellation) {
			inputAssemblyState.topology = VK_PRIMITIVE_TOPOLOGY_PATCH_LIST;
			pipelineCI.pTessellationState = &tessellationState;
			shaderStages[2] = loadShader(getShadersPath() + "pipelinestatistics/scene.tesc.spv", VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT);
			shaderStages[3] = loadShader(getShadersPath() + "pipelinestatistics/scene.tese.spv", VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT);
		}

		pipelineCI.stageCount = static_cast<uint32_t>(shaderStages.size());
		pipelineCI.pStages = shaderStages.data();
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipeline));
	}

	void prepare()
	{
		VulkanExampleBase::prepare();
		// Prepare per-frame ressources
		frameObjects.resize(getFrameCount());
		for (FrameObjects& frame : frameObjects) {
			createBaseFrameObjects(frame);
			// Uniform buffers
			VK_CHECK_RESULT(vulkanDevice->createAndMapBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &frame.uniformBuffer, sizeof(UniformData)));
		}
		loadAssets();
		createQueryPool();
		createDescriptors();
		createPipelines();
		prepared = true;
	}

	virtual void render()
	{
		FrameObjects currentFrame = frameObjects[getCurrentFrameIndex()];

		VulkanExampleBase::prepareFrame(currentFrame);

		// Read back the pipeline statistics query results from the last frame
		uint32_t count = static_cast<uint32_t>(pipelineStats.size());
		vkGetQueryPoolResults(
			device,
			queryPool,
			0,
			1,
			count * sizeof(uint64_t),
			pipelineStats.data(),
			sizeof(uint64_t),
			VK_QUERY_RESULT_64_BIT);

		// Update uniform-buffers for the next frame
		if (!paused || camera.updated) {
			UniformData uniformData;
			uniformData.projection = camera.matrices.perspective;
			uniformData.modelview = camera.matrices.view;
			memcpy(currentFrame.uniformBuffer.mapped, &uniformData, sizeof(uniformData));
		}

		// Build the command buffer
		const VkCommandBuffer commandBuffer = currentFrame.commandBuffer;
		const VkCommandBufferBeginInfo commandBufferBeginInfo = getCommandBufferBeginInfo();
		const VkRect2D renderArea = getRenderArea();
		const VkViewport viewport = getViewport();
		const VkRenderPassBeginInfo renderPassBeginInfo = getRenderPassBeginInfo(renderPass, defaultClearValues);
		VK_CHECK_RESULT(vkBeginCommandBuffer(commandBuffer, &commandBufferBeginInfo));
		// Reset the timestamp query pool
		vkCmdResetQueryPool(commandBuffer, queryPool, 0, static_cast<uint32_t>(pipelineStats.size()));

		vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
		vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
		vkCmdSetScissor(commandBuffer, 0, 1, &renderArea);

		// Start capture of pipeline statistics
		vkCmdBeginQuery(commandBuffer, queryPool, 0, 0);
		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &currentFrame.descriptorSet, 0, nullptr);
		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

		// Draw a grid of models
		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &currentFrame.descriptorSet, 0, NULL);
		models.objects[models.objectIndex].bindBuffers(commandBuffer);
		for (int32_t y = 0; y < gridSize; y++) {
			for (int32_t x = 0; x < gridSize; x++) {
				glm::vec3 pos = glm::vec3(float(x - (gridSize / 2.0f)) * 2.5f, 0.0f, float(y - (gridSize / 2.0f)) * 2.5f);
				vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::vec3), &pos);
				models.objects[models.objectIndex].draw(commandBuffer);
			}
		}

		// End the capture of pipeline statistics
		vkCmdEndQuery(commandBuffer, queryPool, 0);

		UIOverlay.draw(commandBuffer);
		vkCmdEndRenderPass(commandBuffer);
		VK_CHECK_RESULT(vkEndCommandBuffer(commandBuffer));

		VulkanExampleBase::submitFrame(currentFrame);
	}

	virtual void OnUpdateUIOverlay(vks::UIOverlay *overlay)
	{
		bool recreatePipelines = false;
		if (overlay->header("Settings")) {
			overlay->comboBox("Object type", &models.objectIndex, models.names);
			overlay->sliderInt("Grid size", &gridSize, 1, 10);
			std::vector<std::string> cullModeNames = { "None", "Front", "Back", "Back and front" };
			if (overlay->comboBox("Cull mode", &cullMode, cullModeNames)) {
				recreatePipelines = true;
			}
			if (overlay->checkBox("Blending", &blending)) {
				recreatePipelines = true;
			}
			if (deviceFeatures.fillModeNonSolid) {
				if (overlay->checkBox("Wireframe", &wireframe)) {
					recreatePipelines = true;
				}
			}
			if (deviceFeatures.tessellationShader) {
				if (overlay->checkBox("Tessellation", &tessellation)) {
					recreatePipelines = true;
				}
			}
			if (overlay->checkBox("Discard", &discard)) {
				recreatePipelines = true;
			}
		}
		if (!pipelineStats.empty()) {
			if (overlay->header("Pipeline statistics")) {
				for (auto i = 0; i < pipelineStats.size(); i++) {
					std::string caption = pipelineStatNames[i] + ": %d";
					overlay->text(caption.c_str(), pipelineStats[i]);
				}
			}
		}
		if (recreatePipelines) {
			createPipelines();
		}
	}

};

VULKAN_EXAMPLE_MAIN()