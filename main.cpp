#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES) // 取决于是否启用了CPP20模块
#	include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp; // 对于旧版VSC进行回退
#endif
#include <GLFW/glfw3.h>

constexpr uint32_t WIDTH  = 800;
constexpr uint32_t HEIGHT = 600;

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

		// 从 GLFW 获取所需的实例扩展。
		uint32_t glfwExtensionCount = 0;
		auto glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

		// 检查 Vulkan 实现是否支持所需的 GLFW 扩展。
		auto extensionProperties = context.enumerateInstanceExtensionProperties();
		for (uint32_t i = 0; i < glfwExtensionCount; ++i)
		{
			if (std::ranges::none_of(extensionProperties,
			                         [glfwExtension = glfwExtensions[i]](auto const &extensionProperty)
			                         { return std::strcmp(extensionProperty.extensionName, glfwExtension) == 0; }))
			{
				throw std::runtime_error("Required GLFW extension not supported: " + std::string(glfwExtensions[i]));
			}
		}

		vk::InstanceCreateInfo createInfo{
		    .pApplicationInfo        = &appInfo,
		    .enabledExtensionCount   = glfwExtensionCount,
		    .ppEnabledExtensionNames = glfwExtensions};
		instance = vk::raii::Instance(context, createInfo); // 创建 Vulkan 实例
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
