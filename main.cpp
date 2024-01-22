#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <iostream>
#include <fstream>  // shader module：读取文件
#include <stdexcept>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <optional>  // 物理设备
#include <set>  // 窗口表面：去重物理设备用于逻辑队列创建
#include <vulkan/vk_enum_string_helper.h>  // 帮助把VkResult转换成string，string_VkResult

const uint32_t WIDTH = 800;
const uint32_t HEIGHT = 600;

// 验证层扩展名
const std::vector<const char*> validationLayers = {
    "VK_LAYER_KHRONOS_validation"
};

// 逻辑设备：设备扩展支持
const std::vector<const char*> deviceExtensions = {
    // 逻辑设备：在mac上的vulkan运行时必须开启该extension
#ifdef __APPLE__
    "VK_KHR_portability_subset",
#endif
    VK_KHR_SWAPCHAIN_EXTENSION_NAME  // swapchain：必须开启扩展才支持swapchain
};


#ifdef NDEBUG  // C的宏，assert中也用到这个
const bool enableValidationLayers = false;
#else
const bool enableValidationLayers = true;
#endif

// 验证层：需要扩展函数vkCreateDebugUtilsMessengerEXT来创建message，因为是扩展需要先查询函数地址
VkResult CreateDebugUtilsMessengerEXT(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDebugUtilsMessengerEXT* pDebugMessenger) {
    auto func = (PFN_vkCreateDebugUtilsMessengerEXT) vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
    if (func != nullptr) {
        return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
    } else {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
}

// 验证层：删除message，需要先查询扩展函数地址再调用
void DestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger, const VkAllocationCallbacks* pAllocator) {
    auto func = (PFN_vkDestroyDebugUtilsMessengerEXT) vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
    if (func != nullptr) {
        func(instance, debugMessenger, pAllocator);
    }
}

// 物理设备：保存queuefamily的索引
// std::optional是一个包装器，它不包含任何值直到为其赋值。调用has_value()成员函数来查询它是否包含值
struct QueueFamilyIndices {
    std::optional<uint32_t> graphicsFamily;  // 图形queue family
    std::optional<uint32_t> presentFamily;  // 窗口表面：用于展示的queue family，支持物理设备将图像呈现到创建的surface

    bool isComplete() {
        return graphicsFamily.has_value() && presentFamily.has_value();
    }
};

// swapchain：swapchain和surface不一定兼容，所以需要检查三种属性
// 基本surface能力，包括图像最小/最大数量，图像最小/最大的高度/宽度
// surface格式
// presentation mode
struct SwapChainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};

class HelloTriangleApplication {
public:
    void run() {
        initWindow();
        initVulkan();
        mainLoop();
        cleanup();
    }

private:
    GLFWwindow* window;

    VkInstance instance;
    VkDebugUtilsMessengerEXT debugMessenger;  // 验证层：回调message
    VkSurfaceKHR surface;  // 窗口表面

    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;  // 物理设备
    VkDevice device;  // 逻辑设备

    VkQueue graphicsQueue;  // 逻辑设备：图形队列
    VkQueue presentQueue;  // 窗口表面：展示队列，用于呈现图像给surface

    // swapchain
    VkSwapchainKHR swapChain;
    std::vector<VkImage> swapChainImages;
    VkFormat swapChainImageFormat;
    VkExtent2D swapChainExtent;
    std::vector<VkImageView> swapChainImageViews;  // imageview：需要用view来读取swapchain image

    VkRenderPass renderPass;  // renderpass
    VkPipelineLayout pipelineLayout;  // fixed function：用于传递uniform

    void initWindow() {
        glfwInit();

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

        window = glfwCreateWindow(WIDTH, HEIGHT, "Vulkan", nullptr, nullptr);
    }

    void initVulkan() {
        createInstance();
        setupDebugMessenger();  // 验证层：创建回调message
        createSurface();  // 窗口表面：创建完instance之后立刻创建，因为会影响物理设备选择
        pickPhysicalDevice();  // 物理设备
        createLogicalDevice();  // 逻辑设备
        createSwapChain();  // swapchain
        createImageViews();  // imageview
        createRenderPass();  // renderpass
        createGraphicsPipeline();  // pipeline
    }

    void mainLoop() {
        while (!glfwWindowShouldClose(window)) {
            glfwPollEvents();  // 事件循环处理
        }
    }

    void cleanup() {
        vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
        vkDestroyRenderPass(device, renderPass, nullptr);

        for (auto imageView : swapChainImageViews) {
            vkDestroyImageView(device, imageView, nullptr);
        }

        vkDestroySwapchainKHR(device, swapChain, nullptr);
        vkDestroyDevice(device, nullptr);

        if (enableValidationLayers) {
            DestroyDebugUtilsMessengerEXT(instance, debugMessenger, nullptr);
        }

        vkDestroySurfaceKHR(instance, surface, nullptr);
        vkDestroyInstance(instance, nullptr);

        glfwDestroyWindow(window);

        glfwTerminate();
    }

    void createInstance() {
        // 验证层：检查验证层是否可用
        if (enableValidationLayers && !checkValidationLayerSupport()) {
            throw std::runtime_error("validation layers requested, but not available!");
        }

        VkApplicationInfo appInfo{};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "Hello Triangle";
        appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.pEngineName = "No Engine";  // 不使用特定引擎，可以写上自己引擎的名字
        appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.apiVersion = VK_API_VERSION_1_3;  // vulkan版本，这里不能使用1.0否则会要求扩展VK_KHR_get_physical_device_properties2

        VkInstanceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo = &appInfo;
        
        auto extensions = getRequiredExtensions();

#ifdef __APPLE__
        createInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif
        
        createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        createInfo.ppEnabledExtensionNames = extensions.data();

        // 验证层：在instance中开启验证层
        VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
        if (enableValidationLayers) {
            createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
            createInfo.ppEnabledLayerNames = validationLayers.data();
            
            // 验证层：验证层使用了vkGetInstanceProcAddr依赖于instance
            // 比如回调message必须在instance删除前被删除，那么无法验证vkCreateInstance和vkDestroyInstance
            // 解决方法是把VkDebugUtilsMessengerCreateInfoEXT设置给pNext，就能验证instance的创建和杀出
            populateDebugMessengerCreateInfo(debugCreateInfo);
            createInfo.pNext = (VkDebugUtilsMessengerCreateInfoEXT*) &debugCreateInfo;
        } else {
            createInfo.enabledLayerCount = 0;

            createInfo.pNext = nullptr;
        }

        if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS) {  // 可以用string_VkResult打印VkResult
            throw std::runtime_error("failed to create instance!");
        }
    }

    // 验证层：创建回调message的信息
    void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo) {
        createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        createInfo.pfnUserCallback = debugCallback;
    }

    // 验证层：创建回调message
    void setupDebugMessenger() {
        if (!enableValidationLayers) return;

        VkDebugUtilsMessengerCreateInfoEXT createInfo;
        populateDebugMessengerCreateInfo(createInfo);

        // 这里不是vk开头的函数，因为本来需要用vkCreateDebugUtilsMessengerEXT来创建，但是因为是扩展所以需要用vkGetInstanceProcAddr来查找函数地址
        if (CreateDebugUtilsMessengerEXT(instance, &createInfo, nullptr, &debugMessenger) != VK_SUCCESS) {
            throw std::runtime_error("failed to set up debug messenger!");
        }
    }

    // 窗口表面：创建surface，surface链接了vulkan和window，也就是没有surface vulkan无法渲然到窗口上。glfw函数做了多平台适配
    void createSurface() {
        if (glfwCreateWindowSurface(instance, window, nullptr, &surface) != VK_SUCCESS) {
            throw std::runtime_error("failed to create window surface!");
        }
    }

    // 物理设备：获取物理设备
    void pickPhysicalDevice() {
        uint32_t deviceCount = 0;
        vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);

        if (deviceCount == 0) {
            throw std::runtime_error("failed to find GPUs with Vulkan support!");
        }

        std::vector<VkPhysicalDevice> devices(deviceCount);
        vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());  // 获得所有物理设备

        for (const auto& device : devices) {
            if (isDeviceSuitable(device)) {  // 检查物理设备是否符合要求
                physicalDevice = device;
                break;
            }
        }

        if (physicalDevice == VK_NULL_HANDLE) {
            throw std::runtime_error("failed to find a suitable GPU!");
        }
    }

    // 逻辑设备：创建逻辑设备
    void createLogicalDevice() {
        QueueFamilyIndices indices = findQueueFamilies(physicalDevice);

        // queue的创建信息
        std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;  // 窗口表面：创建多个queue
        std::set<uint32_t> uniqueQueueFamilies = {  // 窗口表面：使用set去重，如果物理设备是同一个则只保留一个
            indices.graphicsFamily.value(),
            indices.presentFamily.value()
        };

        // 0.0到1.0分配队列优先级来影响Command Buffer执行的调用，即使只有一个queue也是必须的
        float queuePriority = 1.0f;
        for (uint32_t queueFamily : uniqueQueueFamilies) {
            VkDeviceQueueCreateInfo queueCreateInfo{};
            queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queueCreateInfo.queueFamilyIndex = queueFamily;
            queueCreateInfo.queueCount = 1;
            queueCreateInfo.pQueuePriorities = &queuePriority;
            queueCreateInfos.push_back(queueCreateInfo);
        }

        VkPhysicalDeviceFeatures deviceFeatures{};

        // device的创建信息
        VkDeviceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;

        createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
        createInfo.pQueueCreateInfos = queueCreateInfos.data();

        createInfo.pEnabledFeatures = &deviceFeatures;

        // swapchain：开启swapchain拓展，如果是mac也需要mac拓展
        createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
        createInfo.ppEnabledExtensionNames = deviceExtensions.data();

        // 之前device和instance的validation layer设置是分开的，但是现在基本只用instance，这里是为了兼容
        if (enableValidationLayers) {
            createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
            createInfo.ppEnabledLayerNames = validationLayers.data();
        } else {
            createInfo.enabledLayerCount = 0;
        }

        // 创建device，这里不需要instance参与创建
        if (vkCreateDevice(physicalDevice, &createInfo, nullptr, &device) != VK_SUCCESS) {
            throw std::runtime_error("failed to create logical device!");
        }

        // 创建queue
        vkGetDeviceQueue(device, indices.graphicsFamily.value(), 0, &graphicsQueue);
        vkGetDeviceQueue(device, indices.presentFamily.value(), 0, &presentQueue);  // 窗口表面：创建queue
    }

    // swapchain：创建swapchain
    void createSwapChain() {
        SwapChainSupportDetails swapChainSupport = querySwapChainSupport(physicalDevice);
        
        // 获取最佳配置
        VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(swapChainSupport.formats);
        VkPresentModeKHR presentMode = chooseSwapPresentMode(swapChainSupport.presentModes);
        VkExtent2D extent = chooseSwapExtent(swapChainSupport.capabilities);

        // 如果坚持最小image数量可能发生必须等待驱动完成操作才能获取另一个图像进行渲染，所以这里多请求一个图像。同时不超过最大值
        uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;
        if (swapChainSupport.capabilities.maxImageCount > 0 && imageCount > swapChainSupport.capabilities.maxImageCount) {
            imageCount = swapChainSupport.capabilities.maxImageCount;
        }

        VkSwapchainCreateInfoKHR createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        createInfo.surface = surface;

        createInfo.minImageCount = imageCount;
        createInfo.imageFormat = surfaceFormat.format;
        createInfo.imageColorSpace = surfaceFormat.colorSpace;
        createInfo.imageExtent = extent;
        createInfo.imageArrayLayers = 1;  // 指定每个image包含的层数，这里用一层
        createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;  // 指定对图像进行的操作，这里是让渲染的图像转移到swapchain image上

        // 处理跨越多个queuefamily使用image
        QueueFamilyIndices indices = findQueueFamilies(physicalDevice);
        uint32_t queueFamilyIndices[] = {indices.graphicsFamily.value(), indices.presentFamily.value()};

        if (indices.graphicsFamily != indices.presentFamily) {  // 如果队列簇不同，一个image一次由一个queue拥有，另一个queue使用之前需明确转移所有权
            createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            createInfo.queueFamilyIndexCount = 2;
            createInfo.pQueueFamilyIndices = queueFamilyIndices;
        } else {  // 图像可以在多个queue中使用不需要转移所有权
            createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        }

        createInfo.preTransform = swapChainSupport.capabilities.currentTransform;  // 指定图像变换，比如旋转90度
        createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;  // 指定是否应该用alpha通道与其它window混合
        createInfo.presentMode = presentMode;
        createInfo.clipped = VK_TRUE;  // 开启表示不关心被遮挡的像素颜色，比如被其它窗口遮挡

        createInfo.oldSwapchain = VK_NULL_HANDLE;  // swapchain在程序中可能被无效化，比如窗口大小改变需要重新创建，必须在这里指定对旧swapchain引用，这里暂时不需要

        // 创建swapchain
        if (vkCreateSwapchainKHR(device, &createInfo, nullptr, &swapChain) != VK_SUCCESS) {
            throw std::runtime_error("failed to create swap chain!");
        }

        // 创建image
        vkGetSwapchainImagesKHR(device, swapChain, &imageCount, nullptr);
        swapChainImages.resize(imageCount);
        vkGetSwapchainImagesKHR(device, swapChain, &imageCount, swapChainImages.data());

        // 保留image格式和分辨率
        swapChainImageFormat = surfaceFormat.format;
        swapChainExtent = extent;
    }

    // imageview: view描述了如何访问图像以及哪一部分，这里为每个swapchain image创建view，创建后图像可作为color target
    void createImageViews() {
        swapChainImageViews.resize(swapChainImages.size());

        for (size_t i = 0; i < swapChainImages.size(); i++) {
            VkImageViewCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            createInfo.image = swapChainImages[i];
            // type和format描述了如何解释图像数据
            createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            createInfo.format = swapChainImageFormat;
            // 允许调整通道，比如可以都映射到r通道形成单色纹理
            createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
            // subresourcerange描述如何使用图像以及哪一部分，这里用作color target
            createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            createInfo.subresourceRange.baseMipLevel = 0;
            createInfo.subresourceRange.levelCount = 1;
            createInfo.subresourceRange.baseArrayLayer = 0;
            createInfo.subresourceRange.layerCount = 1;

            if (vkCreateImageView(device, &createInfo, nullptr, &swapChainImageViews[i]) != VK_SUCCESS) {
                throw std::runtime_error("failed to create image views!");
            }
        }
    }

    // renderpass：在pipeline之前创建
    void createRenderPass() {
        // attachment
        VkAttachmentDescription colorAttachment{};
        colorAttachment.format = swapChainImageFormat;  // 与swapchain image一致
        colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;  // 没有开启抗锯齿所以是1
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;  // 渲染前处理attachment，适用于color和depth。这里做清除
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;  // 渲染后处理attachment，适用于color和depth。这里将内容存储在内存中
        colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;  // 适用于stencil
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;  // 适用于stencil
        // layout问题：cpu往往线性读写图像，而gpu适合tile的方式读写图像，需要不同布局
        colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;  // 指定渲染前图像的布局。这里是不关心布局，也意味着图像会被清除
        colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;  // 指定renderpass完成后转换的布局，这里是在swapchain用于展示

        // subpass：一个subpass依赖于先前subpass渲染结果，比如用于处理一系列后处理效果
        VkAttachmentReference colorAttachmentRef{};  // 用于subpass引用一个或多个attachment
        colorAttachmentRef.attachment = 0;  // 索引，这里是指fs中layout(location = 0) out vec4
        colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;  // vulkan未来可能支持compute subpass所以和graphics区分
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorAttachmentRef;  // fs结果写入这个attachment

        // renderpass
        VkRenderPassCreateInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = 1;
        renderPassInfo.pAttachments = &colorAttachment;
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;

        if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS) {
            throw std::runtime_error("failed to create render pass!");
        }
    }

    // pipeline：创建pipeline
    void createGraphicsPipeline() {
        auto vertShaderCode = readFile("/Users/sichaoshu/workspace/VulkanTutorial/VulkanTutorial/shaders/vert.spv");
        auto fragShaderCode = readFile("/Users/sichaoshu/workspace/VulkanTutorial/VulkanTutorial/shaders/frag.spv");
        
        // shader module在pipeline创建之后可以被销毁，因为创建管道时被编译和链接到机器码
        VkShaderModule vertShaderModule = createShaderModule(vertShaderCode);
        VkShaderModule fragShaderModule = createShaderModule(fragShaderCode);

        VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
        vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertShaderStageInfo.module = vertShaderModule;
        vertShaderStageInfo.pName = "main";  // 指定调用函数，这意味着可以将多个shader组合在一个shader module中并用不同入口点区分

        VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
        fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragShaderStageInfo.module = fragShaderModule;
        fragShaderStageInfo.pName = "main";

        VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};
        
        // fixed function：描述顶点数据格式
        VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
        vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInputInfo.vertexBindingDescriptionCount = 0;  // 主要描述数据之间的间距以及数据是逐顶点还是逐实例
        vertexInputInfo.vertexAttributeDescriptionCount = 0;  // 传递给顶点着色器的属性的类型，从哪个bind加载它们以及在哪个偏移量

        // fixed function：决定图元类型以及是否开启图元复用
        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        inputAssembly.primitiveRestartEnable = VK_FALSE;

        // fixed function：设置viewport和scissor，viewport和window分辨率一样，定义图像到framebuffer转换，scissor定义实际存储区域
        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        // fixed function：设置光栅化器
        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.depthClampEnable = VK_FALSE;  // 如果是ture则远近平面片段会被截断到深度范围而不是直接丢弃
        rasterizer.rasterizerDiscardEnable = VK_FALSE;  // 如果为ture则图形不会通过光栅化，不会有输出
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;  // 确定如何生成片段，比如可以选择输出线框或者输出顶点
        rasterizer.lineWidth = 1.0f;  // 线条粗细
        rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;  // 面剔除，这里剔除背面
        rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;  // 视为正面的顶点顺序，这里顺时针
        rasterizer.depthBiasEnable = VK_FALSE;  // 主要是用于shadow map的深度偏移，可以添加常值或者片段斜率作为偏移量

        // fixed function：硬件抗锯齿
        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.sampleShadingEnable = VK_FALSE;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        // fixed function：片段着色器返回需要与framebuffer混合，这里进行设置
        VkPipelineColorBlendAttachmentState colorBlendAttachment{};  // 每个attachment的混合设置
        // 控制blend后颜色写入通道，这里开启rgba
        colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachment.blendEnable = VK_FALSE;  // 是否开启blend

        VkPipelineColorBlendStateCreateInfo colorBlending{};  // 全局混合设置，开启后将禁用上面的设置
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.logicOpEnable = VK_FALSE;
        colorBlending.logicOp = VK_LOGIC_OP_COPY;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;
        colorBlending.blendConstants[0] = 0.0f;
        colorBlending.blendConstants[1] = 0.0f;
        colorBlending.blendConstants[2] = 0.0f;
        colorBlending.blendConstants[3] = 0.0f;


        // fixed function：大部分管道状态需要预编译进PSO中，而有些dynamic state可以直接更改而不需要重现创建管道
        std::vector<VkDynamicState> dynamicStates = {  // viewport和scissor设置成动态状态
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR
        };
        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();

        // fixed function：shader中的uniform需要pipelinelayout来指定
        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = 0;
        pipelineLayoutInfo.pushConstantRangeCount = 0;  // 指定了pushConstant，也是传递uniform的方式

        if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
            throw std::runtime_error("failed to create pipeline layout!");
        }

        vkDestroyShaderModule(device, fragShaderModule, nullptr);
        vkDestroyShaderModule(device, vertShaderModule, nullptr);
    }

    // shader module：把shader代码包装进VkShaderModule
    VkShaderModule createShaderModule(const std::vector<char>& code) {
        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = code.size();
        createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());  // 字节码指针式uint32_t所以要转换

        VkShaderModule shaderModule;
        if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
            throw std::runtime_error("failed to create shader module!");
        }

        return shaderModule;
    }

    // swapchain：寻找最佳surface配置
    VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats) {
        // 查看是否有rgba各8位总共32位的通道格式
        // 查看是否有色彩空间支持srgb
        for (const auto& availableFormat : availableFormats) {
            if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB && availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                return availableFormat;
            }
        }

        return availableFormats[0];
    }

    // swapchain：寻找最佳presentation mode配置。包含四种模式
    // 补充：显示器刷新时有一段时间需要重置状态，产生垂直空白vertical blank
    // VK_PRESENT_MODE_IMMEDIATE_KHR：应用程序提交的图像立刻上屏，可能导致画面撕裂
    // VK_PRESENT_MODE_FIFO_KHR：swapchain是队列，显示器刷新时，也就是垂直空白时，从中取图像，如果队列满了则应用程序需要等待，类似垂直同步
    // VK_PRESENT_MODE_FIFO_RELAXED_KHR：和上一个模式一样，除了在应用程序有延迟并且最后一次垂直空白时队列为空，图像不是等待下一个垂直空白而是立即传输，可能画面撕裂
    // VK_PRESENT_MODE_MAILBOX_KHR：三重缓冲，第二种模式变体，队列满时不阻塞程序，而是将队列中图像替换成最新图像
    VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes) {
        // 尽量选择三重缓冲
        for (const auto& availablePresentMode : availablePresentModes) {
            if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
                return availablePresentMode;
            }
        }

        return VK_PRESENT_MODE_FIFO_KHR;
    }

    // swapchain：寻找最佳surface基本能力，extent主要是配置image的分辨率
    VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities) {
        if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
            return capabilities.currentExtent;  // 一般分辨率等于window分辨率，直接返回
        } else {  // 有些window管理器返回最大值，表示在minImageExtent和maxImageExtent之间选择和窗口最匹配分辨率
            int width, height;
            glfwGetFramebufferSize(window, &width, &height);  // 查询window分辨率

            VkExtent2D actualExtent = {
                static_cast<uint32_t>(width),
                static_cast<uint32_t>(height)
            };

            actualExtent.width = std::clamp(actualExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
            actualExtent.height = std::clamp(actualExtent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);

            return actualExtent;
        }
    }

    // swapchain：查询swapchain信息，已确定是否和surface兼容
    SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device) {
        SwapChainSupportDetails details;
        
        // 确定swapchain支持的能力
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &details.capabilities);

        // 确定surface支持的格式
        uint32_t formatCount;
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);

        if (formatCount != 0) {
            details.formats.resize(formatCount);
            vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, details.formats.data());
        }

        // 查询presentation mode
        uint32_t presentModeCount;
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, nullptr);

        if (presentModeCount != 0) {
            details.presentModes.resize(presentModeCount);
            vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, details.presentModes.data());
        }

        return details;
    }

    bool isDeviceSuitable(VkPhysicalDevice device) {
        QueueFamilyIndices indices = findQueueFamilies(device);

        // swapchain：检查设备extension支持，这里主要要检查swapchain是否支持
        bool extensionsSupported = checkDeviceExtensionSupport(device);

        bool swapChainAdequate = false;
        if (extensionsSupported) {
            SwapChainSupportDetails swapChainSupport = querySwapChainSupport(device);
            // swapchain：只需要用到一种surface格式以及presentation mode
            swapChainAdequate = !swapChainSupport.formats.empty() && !swapChainSupport.presentModes.empty();
        }

        return indices.isComplete() && extensionsSupported && swapChainAdequate;
    }

    // swapchain：检查设备是否支持所有extension
    bool checkDeviceExtensionSupport(VkPhysicalDevice device) {
        uint32_t extensionCount;
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

        std::vector<VkExtensionProperties> availableExtensions(extensionCount);
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

        std::set<std::string> requiredExtensions(deviceExtensions.begin(), deviceExtensions.end());

        // 查询设备是否有所有extension
        for (const auto& extension : availableExtensions) {
            requiredExtensions.erase(extension.extensionName);
        }

        return requiredExtensions.empty();
    }

    // 物理设备：检查设备提供的queuefamily
    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device) {
        QueueFamilyIndices indices;

        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

        int i = 0;
        for (const auto& queueFamily : queueFamilies) {
            if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {  // 检查queuefamily是否支持图形功能
                indices.graphicsFamily = i;
            }

            // 窗口表面：检查queuefamily是否支持呈现功能
            VkBool32 presentSupport = false;
            vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);

            if (presentSupport) {
                indices.presentFamily = i;
            }

            if (indices.isComplete()) {
                break;
            }

            i++;
        }

        return indices;
    }

    std::vector<const char*> getRequiredExtensions() {
        uint32_t glfwExtensionCount = 0;
        const char** glfwExtensions;
        glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);  // vulkan对于平台没有api支持，需要添加扩展，使用glfw函数返回扩展信息

        std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

        // 验证层：应用接受验证层debug信息需要使用回调，需要开启扩展
        // 验证层需要链接libVkLayer_khronos_validation.dylib
        if (enableValidationLayers) {
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }

        // mac必须开启的extension，否则创建instance报驱动错误。注意这里必须链接libmoltanvk.dylib否则会继续报错
#ifdef __APPLE__  // 这是c语言的操作系统预定义宏，表示macos和ios，也可以用__MACH__
        extensions.emplace_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
#endif

        return extensions;
    }
        
    // 验证层：检查验证层是否可用，先获取数量再获取可用layer
    bool checkValidationLayerSupport() {
        uint32_t layerCount;
        vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

        std::vector<VkLayerProperties> availableLayers(layerCount);
        vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

        for (const char* layerName : validationLayers) {
            bool layerFound = false;

            for (const auto& layerProperties : availableLayers) {
                if (strcmp(layerName, layerProperties.layerName) == 0) {
                    layerFound = true;
                    break;
                }
            }

            if (!layerFound) {
                return false;
            }
        }

        return true;
    }

    // shader module：读取spriv文件
    static std::vector<char> readFile(const std::string& filename) {
        // ate：从文件末端读取，好处是可以使用读取位置确定文件大小并分配缓冲
        // binary：作为二进制读取，避免文本转换
        std::ifstream file(filename, std::ios::ate | std::ios::binary);

        if (!file.is_open()) {
            throw std::runtime_error("failed to open file!");
        }

        size_t fileSize = (size_t) file.tellg();  // tellg返回当前指针位置，因为ate所以返回了文件大小
        std::vector<char> buffer(fileSize);

        file.seekg(0);  // 从0开始读取
        file.read(buffer.data(), fileSize);

        file.close();

        return buffer;
    }

    // 验证层：VKAPI_ATTR和VKAPI_CALL用于确保函数具有正确签名，让vulkan能正确调用
    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageType, const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData) {
        std::cerr << "validation layer: " << pCallbackData->pMessage << std::endl;

        return VK_FALSE;
    }
};

int main() {
    HelloTriangleApplication app;

    try {
        app.run();
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
