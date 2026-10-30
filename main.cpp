#include <algorithm> // Necessary for std::clamp
#include <cassert> // Necessary for uint32_t
#include <limits> // Necessary for std::numeric_limits
#include <cstdint>
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
		createSwapChain();
		createImageViews();
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

// 另外你那句 // 你是什么AI 的注释里有个事实错误：那不是我，我是 GitHub Copilot（DeepSeek V4.1 Flash），那段自述可以删掉了。

	// 创建逻辑设备用于和物理设备对接
	vk::raii::Device device = nullptr; // 保存逻辑设备句柄。
	vk::raii::Queue graphicsQueue = nullptr; // 保存图形队列句柄。设备销毁时，设备队列会被隐式清理，因此 cleanup 中无需做任何事。
	vk::raii::SwapchainKHR swapChain = nullptr; // 来保存 vk::SwapchainKHR 对象及其图像
	std::vector<vk::raii::ImageView> swapChainImageViews; // 用于保存交换链图像视图的句柄（声明在 device 之后，保证先于 device 销毁）
	// 把为交换链图像选定的格式和范围存到成员变量里，后面的章节会用到。
	std::vector<vk::Image> swapChainImages;
	vk::SurfaceFormatKHR swapChainSurfaceFormat;
	vk::Extent2D swapChainExtent;

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

	uint32_t chooseSwapMinImageCount(vk::SurfaceCapabilitiesKHR const &surfaceCapabilities)
	{
		auto minImageCount = std::max(3u, surfaceCapabilities.minImageCount); // 交换链图像数量至少为3，保证有足够的图像用于渲染和显示。minImageCount是表面支持的最小图像数量，通常为2或3。我们选择3是为了避免在渲染时等待图像可用，从而提高帧率和流畅度。
		if ((0 < surfaceCapabilities.maxImageCount) && (surfaceCapabilities.maxImageCount < minImageCount))
		{
			minImageCount = surfaceCapabilities.maxImageCount;
		}
		return minImageCount;
	}

	vk::SurfaceFormatKHR chooseSwapSurfaceFormat(std::vector<vk::SurfaceFormatKHR> const &availableFormats) // 选择交换链表面格式
	{
		const auto formatIt = std::ranges::find_if( // 遍历列表，优先选择 vk::Format::eB8G8R8A8Srgb 和 vk::ColorSpaceKHR::eSrgbNonlinear 的组合，找不到就按照好坏排序
			availableFormats,
			[](auto const &format) { return format.format == vk::Format::eB8G8R8A8Srgb && format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear; }); // SRGB颜色空间是最常用的颜色空间，几乎所有的显示器都使用它。SRGB格式的图像在渲染时会自动进行伽马校正，保证颜色显示正确。B8G8R8A8格式是最常用的32位颜色格式，支持透明度通道。
		return formatIt != availableFormats.end() ? *formatIt : availableFormats[0];
	}

	vk::PresentModeKHR chooseSwapPresentMode(std::vector<vk::PresentModeKHR> const &availablePresentModes) // 关键词：eImmediate（立即传输到屏幕，会撕裂） eFifo（最广泛，每帧取走一张，队列满了要等待。显示器刷新那一刻成为垂直空白） eFifoRelaxed（提交地晚，垂直空白为空，马上提交） eMailbox（队列满了就替换第一个）
	{
		assert(std::ranges::any_of(availablePresentModes, [](auto presentMode) { return presentMode == vk::PresentModeKHR::eFifo; }));
		return std::ranges::any_of(availablePresentModes,
		                           [](vk::PresentModeKHR value) { return vk::PresentModeKHR::eMailbox == value; }) ?
			       vk::PresentModeKHR::eMailbox :
			       vk::PresentModeKHR::eFifo;
	}

	vk::Extent2D chooseSwapExtent(vk::SurfaceCapabilitiesKHR const &capabilities) // 选择交换链图像的分辨率。要适配GLFW窗口的大小，必须使用 glfwGetFramebufferSize() 获取实际的像素尺寸，而不是 glfwGetWindowSize() 获取逻辑尺寸。因为在高DPI屏幕上，逻辑尺寸和像素尺寸可能不一致。
	{
		// currentExtent 只有在上文所述的“未定义”特殊取值下才表示由窗口管理器
		// 让我们自行选择范围；取其他任何值都说明表面已规定了固定范围，
		// 我们必须原样使用。
		if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
		{
			return capabilities.currentExtent;
		}
		int width, height;
		glfwGetFramebufferSize(window, &width, &height);

		return {
			std::clamp<uint32_t>(width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
			std::clamp<uint32_t>(height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height)
		};
	}

	void createSwapChain() // 准备完成后创建交换链
	{
		vk::SurfaceCapabilitiesKHR surfaceCapabilities = physicalDevice.getSurfaceCapabilitiesKHR(*surface); // 该函数在确定支持的能力时，会考虑传入的 vk::SurfaceKHR 窗口表面。所有支持性查询函数都把窗口表面作为第一个参数，因为它是交换链的核心组成部分。
		swapChainExtent                                = chooseSwapExtent(surfaceCapabilities);
		uint32_t minImageCount                         = chooseSwapMinImageCount(surfaceCapabilities); // 选择交换链图像数量

		std::vector<vk::SurfaceFormatKHR> availableFormats = physicalDevice.getSurfaceFormatsKHR(*surface); // 查询支持的表面格式
		swapChainSurfaceFormat                             = chooseSwapSurfaceFormat(availableFormats);
		std::vector<vk::PresentModeKHR> availablePresentModes = physicalDevice.getSurfacePresentModesKHR(*surface); // 查询支持的呈现模式
		// 支持至少一个足够，然后才查询交换链支持情况


		vk::SwapchainCreateInfoKHR swapChainCreateInfo{ .surface = *surface,
														.minImageCount = minImageCount,
														.imageFormat = swapChainSurfaceFormat.format,
														.imageColorSpace = swapChainSurfaceFormat.colorSpace,
														.imageExtent = swapChainExtent, // （可以注释掉看看验证层会发生什么）交换链图像的分辨率，必须在表面支持的范围内。这里我们选择和窗口大小一致的分辨率。
														.imageArrayLayers = 1, // 交换链图像的层数，除非做立体渲染，否则总是1
														.imageUsage = vk::ImageUsageFlagBits::eColorAttachment, // 交换链图像的用途，这里是作为颜色附件使用
														.imageSharingMode = vk::SharingMode::eExclusive, // 交换链图像的共享模式，这里是独占模式，意味着图像只能被一个队列族使用，如果要在多个队列族之间共享，需要使用 vk::SharingMode::eConcurrent
														.preTransform = surfaceCapabilities.currentTransform, // 交换链图像的预变换方式，这里是当前表面变换方式，例如旋转 90° 、水平翻转等
														.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque, // 交换链图像的透明度处理方式，这里是完全不透明
														.presentMode = chooseSwapPresentMode(availablePresentModes),
														.clipped = true}; // 效果不言而喻XD。反正我是不知道是什么意思。文档说是窗口裁剪，可以节约性能。

		swapChain = vk::raii::SwapchainKHR(device, swapChainCreateInfo); // 创建交换链，如此简单
		swapChainImages = swapChain.getImages(); // 取出其中 vk::Image 对象的句柄
	}

	void createImageViews()
	{
		assert(swapChainImageViews.empty());

		vk::ImageViewCreateInfo imageViewCreateInfo{ .viewType         = vk::ImageViewType::e2D, // 你想要几维的渲染？
													.format           = swapChainSurfaceFormat.format, // 交换链图像的格式必须和交换链本身的格式一致
													.components       = { vk::ComponentSwizzle::eIdentity, vk::ComponentSwizzle::eIdentity, vk::ComponentSwizzle::eIdentity, vk::ComponentSwizzle::eIdentity }, // 颜色通道映射，默认不变。不写也行，vk::ComponentMapping 的默认构造函数就是 eIdentity
													.subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 } }; // 描述图像的用途，以及应访问图像的哪一部分。我们的图像将用作颜色目标，不带任何 mipmap 层级，也不带多个层。

													//imageViewCreateInfo.components = {  // 同等于这个
    												//vk::ComponentSwizzle::eIdentity, vk::ComponentSwizzle::eIdentity, vk::ComponentSwizzle::eIdentity, vk::ComponentSwizzle::eIdentity};
													
													// 遍历交换链循环
		for (auto &image : swapChainImages)
		{
			imageViewCreateInfo.image = image; // 关联交换链图像
			swapChainImageViews.emplace_back(device, imageViewCreateInfo); // 创建图像视图，并保存句柄
		}
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
