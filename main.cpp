#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
// #include <memory>
#include <stdexcept>
#include <string>
#include <map>

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES) // 取决于是否启用了CPP20模块
#	include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp; // 对于旧版VSC进行回退
#endif
#define GLFW_INCLUDE_VULKAN // 使用 glfwCreateWindowSurface 时需要
#include <GLFW/glfw3.h>

constexpr uint32_t WIDTH  = 800;
constexpr uint32_t HEIGHT = 600;

// 需要启用的验证层
const std::vector<char const*> validationLayers = {
    "VK_LAYER_KHRONOS_validation"
};

#ifdef NDEBUG // 如果定义了NDEBUG宏，则禁用验证层
constexpr bool enableValidationLayers = false;
#else
constexpr bool enableValidationLayers = true;
#endif

class HelloTriangleApplication
{
  public:
	void run()
	{
		initWindow();
		initVulkan();
		mainLoop();
		cleanup();
	}

  private:
	GLFWwindow *window = nullptr; // 新增私有类成员保存窗口的引用
	vk::raii::Context context;
	vk::raii::Instance instance = nullptr;
	vk::raii::DebugUtilsMessengerEXT debugMessenger = nullptr; // 调试信使，可以有多个
	vk::raii::SurfaceKHR surface = nullptr; // 窗口表面
	vk::raii::PhysicalDevice physicalDevice = nullptr; // 选中的物理设备

	void initWindow() // note: wayland下必须绘制东西才会显示窗口
	{
		glfwInit();

		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API); // 禁用OpenGL上下文
		glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE); // 禁用窗口调整大小

		window = glfwCreateWindow(WIDTH, HEIGHT, "Vulkan", nullptr, nullptr); // 初始化窗口，宽度、高度和标题。选择在哪个显示器上打开窗口，最后一个参数只与 OpenGL 有关
	}

	void initVulkan()
	{
		createInstance();
		setupDebugMessenger();
		createSurface();
		pickPhysicalDevice();
		createLogicalDevice();
	}

	void createSurface()
	{
		VkSurfaceKHR rawSurface = VK_NULL_HANDLE;
		if (glfwCreateWindowSurface(*instance, window, nullptr, &rawSurface) != VK_SUCCESS)
		{
			throw std::runtime_error("failed to create window surface!");
		}
		surface = vk::raii::SurfaceKHR(instance, rawSurface);
	}

	/*
	当然，你也可以把候选项的名字显示出来，让用户自行选择。
	本教程会使用四条标准，物理设备必须全部满足才会被选中：
	- 支持 Vulkan 1.3， 
	- 拥有同时支持图形操作和向窗口表面呈现的队列族， 
	- 支持全部必需的扩展（这里只需要 vk::KHRSwapchainExtensionName），并且
	- 支持全部必需的特性.
*/
	std::vector<const char*> requiredDeviceExtension = {vk::KHRSwapchainExtensionName};
	bool isDeviceSuitable(vk::raii::PhysicalDevice const & physicalDevice) // 举例子，假设我们只需要支持几何着色器的离散GPU设备
	/*{ // 只是想让你了解设备选择流程可以怎样设计。
		auto deviceProperties = physicalDevice.getProperties();
		auto deviceFeatures = physicalDevice.getFeatures();

		if (deviceProperties.deviceType == vk::PhysicalDeviceType::eDiscreteGpu && deviceFeatures.geometryShader) {
			return true;
		}

		return false;
	}*/
	{
		// 检查物理设备是否支持 Vulkan 1.3 API 版本，看物理设备属性中的 apiVersion
		bool supportsVulkan1_3 = physicalDevice.getProperties().apiVersion >= vk::ApiVersion13;

		// 检查是否有队列族同时支持图形操作和向窗口表面呈现（present）。（几乎所有的Vulkan操作都需要队列族！有的显卡只有传输队列族没有图形队列族）
		auto queueFamilies = physicalDevice.getQueueFamilyProperties();
		uint32_t qfpIndex  = 0;
		bool supportsGraphicsAndPresent =
			std::ranges::any_of( queueFamilies,
								 [&physicalDevice, &surface = this->surface, &qfpIndex]( auto const & qfp )
								 {
								 bool const suitable = ( qfp.queueFlags & vk::QueueFlagBits::eGraphics ) && physicalDevice.getSurfaceSupportKHR( qfpIndex, *surface );
								 qfpIndex++;
								 return suitable;
								 } );

		// 检查所有必需的物理设备扩展是否可用，目前只需要vk::KHRSwapchainExtensionName
		auto availableDeviceExtensions = physicalDevice.enumerateDeviceExtensionProperties();
		bool supportsAllRequiredExtensions =
			std::ranges::all_of( requiredDeviceExtension,
								[&availableDeviceExtensions]( auto const & requiredDeviceExtension )
								{
								return std::ranges::any_of( availableDeviceExtensions,
															[requiredDeviceExtension]( auto const & availableDeviceExtension )
															{ return strcmp( availableDeviceExtension.extensionName, requiredDeviceExtension ) == 0; } );
								} );

		// 检查物理设备是否支持必需的特性（着色器绘制参数、动态渲染和扩展动态状态）
		auto features                 = physicalDevice.template getFeatures2<vk::PhysicalDeviceFeatures2,
																			vk::PhysicalDeviceVulkan11Features,
																			vk::PhysicalDeviceVulkan13Features,
																			vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>();
		bool supportsRequiredFeatures = features.template get<vk::PhysicalDeviceVulkan11Features>().shaderDrawParameters &&
										features.template get<vk::PhysicalDeviceVulkan13Features>().dynamicRendering &&
										features.template get<vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>().extendedDynamicState;

		// 物理设备满足全部标准时返回 true
		return supportsVulkan1_3 && supportsGraphicsAndPresent && supportsAllRequiredExtensions && supportsRequiredFeatures;
		}

		void pickPhysicalDevice()
		{
		std::vector<vk::raii::PhysicalDevice> physicalDevices = instance.enumeratePhysicalDevices();
		auto const devIter = std::ranges::find_if( physicalDevices, [&]( auto const & physicalDevice ) { return isDeviceSuitable( physicalDevice ); } );
		if ( devIter == physicalDevices.end() )
		{
			throw std::runtime_error( "failed to find a suitable GPU!" );
		}
		physicalDevice = *devIter;
	}

	// 只是想让你了解设备选择流程可以怎样设计。
	void pickPhysicalDevice1() // 选择物理设备示例
	{
		auto physicalDevices = vk::raii::PhysicalDevices( instance );
		if (physicalDevices.empty())
		{
			throw std::runtime_error( "failed to find GPUs with Vulkan support!" );
		}

		// 用有序 map 自动按分数递增的顺序排列候选设备
		std::multimap<int, vk::raii::PhysicalDevice> candidates;

		for (const auto& pd : physicalDevices)
		{
			auto deviceProperties = pd.getProperties();
			auto deviceFeatures = pd.getFeatures();
			uint32_t score = 0;

			// 独立显卡有明显的性能优势
			if (deviceProperties.deviceType == vk::PhysicalDeviceType::eDiscreteGpu) {
				score += 1000;
			}

			// 纹理的最大可能尺寸影响画面质量
			score += deviceProperties.limits.maxImageDimension2D;

			// 没有几何着色器，应用就无法工作
			if (!deviceFeatures.geometryShader)
			{
			continue;
			}
			candidates.insert(std::make_pair(score, pd));
		}

		// 检查最好的候选设备是否至少可用
		if (!candidates.empty() && candidates.rbegin()->first > 0)
		{
			physicalDevice = candidates.rbegin()->second;
		}
		else
		{
			throw std::runtime_error("failed to find a suitable GPU!");
		}
	}


// 你是什么AI 
// 我是一个由OpenAI开发的人工智能语言模型，旨在理解和生成自然语言文本。我可以帮助回答问题、提供信息、生成文本内容以及协助编程等任务。
// 给我讲个故事
// 当然可以！这是一个关于勇气和友谊的故事：
// 从前，在一个宁静的小村庄里，住着一只名叫莉
// 莉的小兔子。莉莉非常喜欢冒险，但她总是一个人行动，因为其他动物都觉得太危险了。一天，莉莉听说森林深处有一颗神奇的宝石，能够实现任何愿望。她决定去寻找这颗宝石。
// 在旅途中，莉莉遇到了一只受伤的小狐狸。小狐狸告诉莉莉，它迷路了，找不到回家的路。莉莉决定帮助小狐狸，于是他们一起踏上了寻找宝石的旅程。经过许多挑战和困难，他们互相支持，逐渐建立了深厚的友谊。
// 最终，他们找到了那颗神奇的宝石。小狐狸许下了一个愿望，希望莉莉能够永远快乐和安全。莉莉感动得流下了眼泪，她意识到，真正的宝藏不是宝石，而是他们之间的友谊和勇气。

	// 创建逻辑设备用于和物理设备对接
	vk::raii::Device device = nullptr; // 保存逻辑设备句柄。
	vk::raii::Queue graphicsQueue = nullptr; // 保存图形队列句柄。设备销毁时，设备队列会被隐式清理，因此 cleanup 中无需做任何事。
	void createLogicalDevice()
	{
		// 指定要创建的队列族和队列数量。这里我们只需要一个图形队列。
		std::vector<vk::QueueFamilyProperties> queueFamilyProperties = physicalDevice.getQueueFamilyProperties();

		// 找到第一个同时支持图形操作和向窗口表面呈现的队列族
		uint32_t queueIndex = ~0u;
		for (uint32_t qfpIndex = 0; qfpIndex < queueFamilyProperties.size(); qfpIndex++)
		{
			if ((queueFamilyProperties[qfpIndex].queueFlags & vk::QueueFlagBits::eGraphics) &&
			    physicalDevice.getSurfaceSupportKHR(qfpIndex, *surface))
			{
				queueIndex = qfpIndex;
				break;
			}
		}
		if (queueIndex == ~0u)
		{
			throw std::runtime_error("Could not find a queue for graphics and present -> terminating");
		}

		// 必须使用浮点数分配优先级，范围0.0-1.0，1.0表示最高优先级。这里我们只需要一个队列，所以只需要一个优先级。
		// 可以在多个线程上分别创建命令缓冲区，然后在主线程上用一次低开销调用把它们全部提交。另外个人电脑单线程CPU目前基本绝迹。
		float queuePriority = 0.5f;
		vk::DeviceQueueCreateInfo deviceQueueCreateInfo { .queueFamilyIndex = queueIndex, .queueCount = 1, .pQueuePriorities = &queuePriority };
		vk::PhysicalDeviceFeatures deviceFeatures; // 目前我们不需要任何特性，所以定义它、让所有字段保持 vk::False 即可。
		// 创建一条特性结构体链
		vk::StructureChain<vk::PhysicalDeviceFeatures2, // 结构链。通过设置各结构体之间的 pNext 指针，自动把它们连接起来，省去了手动逐个链接的麻烦。
						vk::PhysicalDeviceVulkan11Features,
						vk::PhysicalDeviceVulkan13Features,
						vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>
			featureChain = {
				{},                                    // vk::PhysicalDeviceFeatures2（暂时为空）
				{.shaderDrawParameters = true},        // 启用 Vulkan 1.1 的着色器绘制参数
				{.dynamicRendering = true},            // 启用 Vulkan 1.3 的动态渲染
				{.extendedDynamicState = true}         // 启用该扩展的扩展动态状态
			};
		std::vector<const char*> requiredDeviceExtension = { // 这个扩展是是把渲染好的图像呈现到窗口所必需的。
  	  	vk::KHRSwapchainExtensionName};

		// 填写结构体
		vk::DeviceCreateInfo deviceCreateInfo{ // 如今不再区分区分实例级和设备级的验证层，enabledLayerCount 和 ppEnabledLayerNames 字段会被最新的实现忽略。
			// featureChain.get<T>() 取得链中第一个结构体（即 vk::PhysicalDeviceFeatures2）的引用，
			// 把它的地址赋给 pNext，作为整条特性链的入口。
			// 由于 vk::StructureChain 已经自动设置好链中相邻结构体之间的 pNext 指针，
			// Vulkan 顺着这条 pNext 指针链就能看到我们想启用的所有特性，无需再逐个手动链接。
			.pNext                   = &featureChain.get<vk::PhysicalDeviceFeatures2>(),
			.queueCreateInfoCount    = 1,
			.pQueueCreateInfos       = &deviceQueueCreateInfo,
			.enabledExtensionCount   = static_cast<uint32_t>(requiredDeviceExtension.size()),
			.ppEnabledExtensionNames = requiredDeviceExtension.data()
		};
		// 继续创建逻辑设备，并把特性链接到它上面
		device = vk::raii::Device(physicalDevice, deviceCreateInfo);

		// 用于保存图形队列的句柄的类
		graphicsQueue = vk::raii::Queue(device, queueIndex, 0); // 可以用 vk::raii::Queue 构造函数取得某个队列族的队列句柄。参数是逻辑设备、队列族索引和队列索引。由于我们只从这个族里创建一个队列，队列索引直接用 0

	}

	void mainLoop()
	{
		while (!glfwWindowShouldClose(window)) // 检查窗口是否应该关闭
		{
			glfwPollEvents();
		}
	}

	void cleanup()
	{
		glfwDestroyWindow(window); // 销毁窗口

		glfwTerminate(); // 终止GLFW库
	}

	void createInstance()
	{
		constexpr vk::ApplicationInfo appInfo{.pApplicationName   = "Hello Triangle",
		                                      .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
		                                      .pEngineName        = "No Engine",
		                                      .engineVersion      = VK_MAKE_VERSION(1, 0, 0),
		                                      .apiVersion         = vk::ApiVersion14};

		// 获取所需的层
		std::vector<char const*> requiredLayers;
		if (enableValidationLayers)
		{
			requiredLayers.assign(validationLayers.begin(), validationLayers.end());
		}

		// 检查 Vulkan 实现是否支持所需的层。
		auto layerProperties = context.enumerateInstanceLayerProperties();
		auto unsupportedLayerIt = std::ranges::find_if(requiredLayers,
		                                               [&layerProperties](auto const &requiredLayer) {
			                                               return std::ranges::none_of(layerProperties,
			                                                                           [requiredLayer](auto const &layerProperty) { return std::strcmp(layerProperty.layerName, requiredLayer) == 0; });
		                                               });
		if (unsupportedLayerIt != requiredLayers.end())
		{
			throw std::runtime_error("Required layer not supported: " + std::string(*unsupportedLayerIt));
		}

		// 获取所需的扩展。
		auto requiredExtensions = getRequiredInstanceExtensions();

		// 检查 Vulkan 实现是否支持所需的扩展。
		auto extensionProperties = context.enumerateInstanceExtensionProperties();
		auto unsupportedPropertyIt =
		    std::ranges::find_if(requiredExtensions,
		                         [&extensionProperties](auto const &requiredExtension) {
			                         return std::ranges::none_of(extensionProperties,
			                                                     [requiredExtension](auto const &extensionProperty) { return std::strcmp(extensionProperty.extensionName, requiredExtension) == 0; });
		                         });
		if (unsupportedPropertyIt != requiredExtensions.end())
		{
			throw std::runtime_error("Required extension not supported: " + std::string(*unsupportedPropertyIt));
		}

		// 层和扩展都已就绪，这里才真正启用它们。
		vk::InstanceCreateInfo createInfo{
		    .pApplicationInfo        = &appInfo,
		    .enabledLayerCount       = static_cast<uint32_t>(requiredLayers.size()),
		    .ppEnabledLayerNames     = requiredLayers.data(),
		    .enabledExtensionCount   = static_cast<uint32_t>(requiredExtensions.size()),
		    .ppEnabledExtensionNames = requiredExtensions.data()};
		instance = vk::raii::Instance(context, createInfo); // 创建 Vulkan 实例
	}

	void setupDebugMessenger()
	{
		if (!enableValidationLayers) return;

		// 只关心可能导致应用问题的消息，因此只请求 eWarning 和 eError
		vk::DebugUtilsMessageSeverityFlagsEXT severityFlags(vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
		                                                    vk::DebugUtilsMessageSeverityFlagBitsEXT::eError);
		// 消息类型这里全部启用
		vk::DebugUtilsMessageTypeFlagsEXT messageTypeFlags(vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
		                                                   vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance |
		                                                   vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation);
		vk::DebugUtilsMessengerCreateInfoEXT debugUtilsMessengerCreateInfoEXT{.messageSeverity = severityFlags,
		                                                                     .messageType     = messageTypeFlags,
		                                                                     .pfnUserCallback = &debugCallback};
		debugMessenger = instance.createDebugUtilsMessengerEXT(debugUtilsMessengerCreateInfoEXT); // 注册回调
	}

	std::vector<const char*> getRequiredInstanceExtensions() // 获取并返回所需的实例扩展
	{
		uint32_t glfwExtensionCount = 0;
		auto glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount); // 必须要有GLFW的扩展

		std::vector extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);
		if (enableValidationLayers)
		{
			extensions.push_back(vk::EXTDebugUtilsExtensionName); // 筛选你想看的。等价于字面量字符串 "VK_EXT_debug_utils"。使用这个宏可以避免拼写错误。因为这个常量名大小写明确并且是由 Vulkan-Hpp 提供的编译器方便报错。
		}

		return extensions;
	}

	static VKAPI_ATTR vk::Bool32 VKAPI_CALL debugCallback(vk::DebugUtilsMessageSeverityFlagBitsEXT       severity, // 消息严重程度：eVerbose / eInfo / eWarning / eError
	                                                      vk::DebugUtilsMessageTypeFlagsEXT              type, // 消息类型：eGeneral / eValidation / ePerformance
	                                                      const vk::DebugUtilsMessengerCallbackDataEXT * pCallbackData, // 消息详情，其中 pMessage 是具体的消息字符串
	                                                      void *                                         pUserData) // 创建信使时通过 pUserData 传入的自定义数据
	{
		// 因为类型是 FlagBits 而不是 Flags，回调每次只携带一个严重程度位，所以用 == 而不是 & 判断
		if (severity == vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning ||
		    severity == vk::DebugUtilsMessageSeverityFlagBitsEXT::eError)
		{
			std::cerr << "validation layer: type " << to_string(type) << " msg: " << pCallbackData->pMessage << std::endl;
		}

		return vk::False; // 返回 vk::True 会中止触发该消息的 Vulkan 调用，一般只在测试验证层本身时使用
	}
};

int main()
{
	try
	{
		HelloTriangleApplication app;
		app.run();
	}
	catch (const std::exception &e)
	{
		std::cerr << e.what() << std::endl;
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
