#define SH_BASE_IMPLEMENTATION
#include "sh_base.h"
#define SH_STRING_BUILDER_IMPLEMENTATION
#include "sh_string_builder.h"

typedef enum
{
    NodeTypeObject  = 0,
    NodeTypeArray   = 1,
    NodeTypeString  = 2,
    NodeTypeInteger = 3,
    NodeTypeFloat   = 4,
    NodeTypeBoolean = 5,
} NodeType;

typedef struct Node Node;

struct Node
{
    NodeType type;
    Node *next;
    Node *first;
    Node *last;
    ShString name;
    int32_t count;
    bool compressed;

    union
    {
        ShString _str;
        int64_t _integer64;
        double _float64;
        bool _bool;
    } value;
};

typedef Node *(*InfoCommandFunc)(ShThreadContext *thread_context, ShAllocator, void *, ShString, Node *);

typedef struct
{
    ShString name;
    InfoCommandFunc func;
} InfoCommand;

typedef void *(*BeginContextFunc)(ShAllocator);
typedef void (*EndContextFunc)(ShAllocator, void *);

typedef enum
{
    OutputFormatJson = 0,
    OutputFormatYaml = 1,
} OutputFormat;

#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

static void *
c_default_allocator_func(void *allocator_data, ShAllocatorAction action, usize old_size, usize size, void *ptr)
{
    (void) allocator_data;
    (void) old_size;

    void *result = NULL;

    switch (action)
    {
        case SH_ALLOCATOR_ACTION_ALLOC:   result = malloc(size);       break;
        case SH_ALLOCATOR_ACTION_REALLOC: result = realloc(ptr, size); break;
        case SH_ALLOCATOR_ACTION_FREE:    free(ptr);                   break;
    }

    return result;
}

static inline Node *
push_node(ShAllocator allocator, Node *parent, ShString name, NodeType type, bool compressed)
{
    Node *node = sh_alloc_type(allocator, Node);

    node->type = type;
    node->next = NULL;
    node->first = NULL;
    node->last = NULL;
    node->name = name;
    node->count = 0;
    node->compressed = compressed;

    if (parent)
    {
        if (parent->first)
        {
            parent->last->next = node;
        }
        else
        {
            parent->first = node;
        }

        parent->last = node;
        parent->count += 1;
    }

    return node;
}

static inline Node *
push_object(ShAllocator allocator, Node *parent, ShString name, bool compressed)
{
    return push_node(allocator, parent, name, NodeTypeObject, compressed);
}

static inline Node *
push_array(ShAllocator allocator, Node *parent, ShString name, bool compressed)
{
    return push_node(allocator, parent, name, NodeTypeArray, compressed);
}

static inline Node *
push_string(ShAllocator allocator, Node *parent, ShString name, ShString value)
{
    Node *node = push_node(allocator, parent, name, NodeTypeString, false);
    node->value._str = value;
    return node;
}

static inline Node *
push_integer(ShAllocator allocator, Node *parent, ShString name, int64_t value)
{
    Node *node = push_node(allocator, parent, name, NodeTypeInteger, false);
    node->value._integer64 = value;
    return node;
}

static inline Node *
push_float(ShAllocator allocator, Node *parent, ShString name, double value)
{
    Node *node = push_node(allocator, parent, name, NodeTypeFloat, false);
    node->value._float64 = value;
    return node;
}

static inline Node *
push_boolean(ShAllocator allocator, Node *parent, ShString name, bool value)
{
    Node *node = push_node(allocator, parent, name, NodeTypeBoolean, false);
    node->value._bool = value;
    return node;
}

static Node *
handle_sub_command(ShThreadContext *thread_context, ShAllocator allocator,
                   ShString command, ShString name, Node *parent,
                   usize info_command_count, InfoCommand *info_commands,
                   BeginContextFunc begin_context, EndContextFunc end_context)
{
    Node *result = NULL;

    ShString sub_command = sh_string_split_left_on_char(&command, '.');

    if (sub_command.count == 0)
    {
        void *context = NULL;

        if (begin_context)
        {
            // TODO: use temporary allocator
            context = begin_context(allocator);

            if (!context)
            {
                return NULL;
            }
        }

        result = push_object(allocator, parent, name, false);

        for (usize i = 0; i < info_command_count; i += 1)
        {
            InfoCommand *info_cmd = info_commands + i;
            info_cmd->func(thread_context, allocator, context, command, result);
        }

        if (end_context)
        {
            end_context(allocator, context);
        }
    }
    else
    {
        InfoCommand *info_command = NULL;

        for (usize i = 0; i < info_command_count; i += 1)
        {
            InfoCommand *info_cmd = info_commands + i;

            if (sh_string_equal(info_cmd->name, sub_command))
            {
                info_command = info_cmd;
                break;
            }
        }

        if (info_command)
        {
            void *context = NULL;

            if (begin_context)
            {
                // TODO: use temporary allocator
                context = begin_context(allocator);

                if (!context)
                {
                    return NULL;
                }
            }

            result = info_command->func(thread_context, allocator, context, command, parent);

            if (end_context)
            {
                end_context(allocator, context);
            }
        }
    }

    return result;
}

#if SH_PLATFORM_MACOS

#  include <Foundation/Foundation.h>
#  include <Metal/Metal.h>

static Node *
metal_command_devices(ShThreadContext *thread_context, ShAllocator allocator, void *context, ShString command, Node *parent)
{
    Node *devices_node = push_array(allocator, parent, ShStringLiteral("devices"), false);

    NSArray<id<MTLDevice>> *devices = MTLCopyAllDevices();

    for (id<MTLDevice> device in devices)
    {
        Node *device_node = push_object(allocator, devices_node, ShStringLiteral("__device__"), false);

        push_string (allocator, device_node, ShStringLiteral("name"), sh_copy_string(allocator, ShCString([[device name] UTF8String])));
        push_boolean(allocator, device_node, ShStringLiteral("low_power"), [device isLowPower]);

        if (@available(macOS 10.13, *))
        {
            push_boolean(allocator, device_node, ShStringLiteral("removable"), [device isLowPower]);
        }

        push_boolean(allocator, device_node, ShStringLiteral("headless"), [device isHeadless]);

        if (@available(macOS 14.0, *))
        {
            push_string(allocator, device_node, ShStringLiteral("architecture"), sh_copy_string(allocator, ShCString([[[device architecture] name] UTF8String])));
        }

        if (@available(macOS 11.0, *))
        {
            push_boolean(allocator, device_node, ShStringLiteral("supports_raytracing"), [device supportsRaytracing]);
        }

        if (@available(macOS 12.0, *))
        {
            push_boolean(allocator, device_node, ShStringLiteral("supports_raytracing_from_render"), [device supportsRaytracingFromRender]);
        }
    }

    return devices_node;
}

static InfoCommand metal_commands[] = {
    { ShStringLiteral("devices"), metal_command_devices },
};

static Node *
info_command_metal(ShThreadContext *thread_context, ShAllocator allocator, void *context, ShString command, Node *parent)
{
    return handle_sub_command(thread_context, allocator, command, ShStringLiteral("metal"),
                              parent, ShArrayCount(metal_commands), metal_commands,
                              NULL, NULL);
}

#endif // SH_PLATFORM_MACOS

#if SH_PLATFORM_ANDROID || SH_PLATFORM_WINDOWS || SH_PLATFORM_LINUX

#  if SH_PLATFORM_WINDOWS
#    define WIN32_LEAN_AND_MEAN
#    include <windows.h>
#  elif SH_PLATFORM_ANDROID || SH_PLATFORM_LINUX
#    include <dlfcn.h>
#  endif

#  define VK_NO_PROTOTYPES
#  include <vulkan/vulkan.h>

#  define vulkan_global_functions(proc)                          \
    proc(VK_NULL_HANDLE, vkCreateInstance)                       \
    proc(VK_NULL_HANDLE, vkEnumerateInstanceVersion)             \
    proc(VK_NULL_HANDLE, vkEnumerateInstanceLayerProperties)     \
    proc(VK_NULL_HANDLE, vkEnumerateInstanceExtensionProperties)

#  define vulkan_instance_functions(instance, proc)              \
    proc(instance, vkDestroyInstance)                            \
    proc(instance, vkEnumeratePhysicalDevices)                   \
    proc(instance, vkGetPhysicalDeviceProperties)                \
    proc(instance, vkEnumerateDeviceExtensionProperties)

typedef struct
{
#  if SH_PLATFORM_WINDOWS
    HMODULE library_handle;
#  elif SH_PLATFORM_UNIX
    void *library_handle;
#  endif

    VkInstance instance;

    PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr;

#  define vulkan_declare_function(instance, name) PFN_##name name;

    vulkan_global_functions(vulkan_declare_function)
    vulkan_instance_functions(VK_NULL_HANDLE, vulkan_declare_function)

#  undef vulkan_declare_function
} VulkanContext;

static inline ShString
vk_version_to_string(ShThreadContext *thread_context, ShAllocator allocator, uint32_t version)
{
    ShTemporaryMemory temp_memory = sh_begin_temporary_memory(thread_context, 1, &allocator);

    ShStringBuilder string_builder;
    sh_string_builder_init(&string_builder, temp_memory.allocator);
    sh_string_builder_append_unsigned_number(&string_builder, VK_API_VERSION_MAJOR(version), 0, 0, 10, false);
    sh_string_builder_append_u8(&string_builder, '.');
    sh_string_builder_append_unsigned_number(&string_builder, VK_API_VERSION_MINOR(version), 0, 0, 10, false);
    sh_string_builder_append_u8(&string_builder, '.');
    sh_string_builder_append_unsigned_number(&string_builder, VK_API_VERSION_PATCH(version), 0, 0, 10, false);
    ShString result = sh_string_builder_to_string(&string_builder, allocator);

    sh_end_temporary_memory(temp_memory);

    return result;
}

static inline ShString
vk_physical_device_type_to_string(ShThreadContext *thread_context, ShAllocator allocator, VkPhysicalDeviceType device_type)
{
    ShString result = ShStringEmpty;

    ShTemporaryMemory temp_memory = sh_begin_temporary_memory(thread_context, 1, &allocator);

#  define NAME(name) case name: result = ShStringLiteral(#name); break

    switch (device_type)
    {
        NAME(VK_PHYSICAL_DEVICE_TYPE_OTHER);
        NAME(VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU);
        NAME(VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU);
        NAME(VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU);
        NAME(VK_PHYSICAL_DEVICE_TYPE_CPU);

        default:
        {
            ShStringBuilder string_builder;
            sh_string_builder_init(&string_builder, temp_memory.allocator);
            sh_string_builder_append_string(&string_builder, ShStringLiteral("<unknown_device_type: "));
            sh_string_builder_append_unsigned_number(&string_builder, device_type, 0, 0, 10, false);
            sh_string_builder_append_string(&string_builder, ShStringLiteral(">"));
            result = sh_string_builder_to_string(&string_builder, allocator);
        } break;
    }

#  undef NAME

    sh_end_temporary_memory(temp_memory);

    return result;
}

static void *
begin_vulkan_context(ShAllocator allocator)
{
    VulkanContext *vulkan_context = sh_alloc_type(allocator, VulkanContext);

#  if SH_PLATFORM_WINDOWS
    vulkan_context->library_handle = LoadLibraryA("vulkan-1.dll");

    if (!vulkan_context->library_handle)
    {
        sh_free(allocator, vulkan_context);
        return NULL;
    }

    vulkan_context->vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr) GetProcAddress(vulkan_context->library_handle, "vkGetInstanceProcAddr");

#    define CLOSE_LIBRARY_HANDLE(handle) FreeLibrary(handle)

#  elif SH_PLATFORM_UNIX
    vulkan_context->library_handle = dlopen("libvulkan.so.1", RTLD_NOW);

    if (!vulkan_context->library_handle)
    {
        sh_free(allocator, vulkan_context);
        return NULL;
    }

    vulkan_context->vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr) dlsym(vulkan_context->library_handle, "vkGetInstanceProcAddr");

#    define CLOSE_LIBRARY_HANDLE(handle) dlclose(handle)

#  endif

    if (!vulkan_context->vkGetInstanceProcAddr)
    {
        CLOSE_LIBRARY_HANDLE(vulkan_context->library_handle);
        sh_free(allocator, vulkan_context);
        return NULL;
    }

#  define vulkan_load_instance_function(instance, name)                                             \
    do {                                                                                            \
        vulkan_context->name = (PFN_##name) vulkan_context->vkGetInstanceProcAddr(instance, #name); \
        if (!vulkan_context->name)                                                                  \
        {                                                                                           \
            CLOSE_LIBRARY_HANDLE(vulkan_context->library_handle);                                   \
            sh_free(allocator, vulkan_context);                                                     \
            return NULL;                                                                            \
        }                                                                                           \
    } while (0);

vulkan_global_functions(vulkan_load_instance_function)

    VkApplicationInfo application_info;
    application_info.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    application_info.pNext              = 0;
    application_info.pApplicationName   = "system_info";
    application_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    application_info.pEngineName        = "tools";
    application_info.engineVersion      = VK_MAKE_VERSION(1, 0, 0);
    application_info.apiVersion         = VK_API_VERSION_1_1;

    VkInstanceCreateInfo instance_info;
    instance_info.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instance_info.pNext                   = 0;
    instance_info.flags                   = 0;
    instance_info.pApplicationInfo        = &application_info;
    instance_info.enabledLayerCount       = 0;
    instance_info.ppEnabledLayerNames     = 0;
    instance_info.enabledExtensionCount   = 0;
    instance_info.ppEnabledExtensionNames = 0;

    if (vulkan_context->vkCreateInstance(&instance_info, NULL, &vulkan_context->instance) != VK_SUCCESS)
    {
        CLOSE_LIBRARY_HANDLE(vulkan_context->library_handle);
        sh_free(allocator, vulkan_context);
        return NULL;
    }

vulkan_instance_functions(vulkan_context->instance, vulkan_load_instance_function)

#  undef vulkan_load_instance_function

    return vulkan_context;
}

static void
end_vulkan_context(ShAllocator allocator, void *context)
{
    VulkanContext *vulkan_context = (VulkanContext *) context;

    vulkan_context->vkDestroyInstance(vulkan_context->instance, NULL);

#  if SH_PLATFORM_WINDOWS
    FreeLibrary(vulkan_context->library_handle);
#  elif SH_PLATFORM_UNIX
    dlclose(vulkan_context->library_handle);
#  endif

    sh_free(allocator, vulkan_context);
}

static Node *
vulkan_command_version(ShThreadContext *thread_context, ShAllocator allocator, void *context, ShString command, Node *parent)
{
    VulkanContext *vulkan_context = (VulkanContext *) context;

    uint32_t version = VK_API_VERSION_1_0;
    vulkan_context->vkEnumerateInstanceVersion(&version);

    char buf[16];
    size_t len = snprintf(buf, sizeof(buf), "%u.%u.%u", VK_API_VERSION_MAJOR(version),
                          VK_API_VERSION_MINOR(version), VK_API_VERSION_PATCH(version));

    ShString version_str;
    version_str.count = len;
    version_str.data  = (uint8_t *) buf;

    return push_string(allocator, parent, ShStringLiteral("version"), sh_copy_string(allocator, version_str));
}

static Node *
vulkan_command_layers(ShThreadContext *thread_context, ShAllocator allocator, void *context, ShString command, Node *parent)
{
    VulkanContext *vulkan_context = (VulkanContext *) context;

    Node *layers_node = push_array(allocator, parent, ShStringLiteral("layers"), false);

    ShTemporaryMemory temp_memory = sh_begin_temporary_memory(thread_context, 1, &allocator);

    uint32_t layer_count = 0;

    VkResult vk_result;

    vk_result = vulkan_context->vkEnumerateInstanceLayerProperties(&layer_count, NULL);

    if (vk_result != VK_SUCCESS)
    {
        sh_end_temporary_memory(temp_memory);
        return layers_node;
    }

    VkLayerProperties *layers = sh_alloc_array(temp_memory.allocator, VkLayerProperties, layer_count);

    vk_result = vulkan_context->vkEnumerateInstanceLayerProperties(&layer_count, layers);

    if (vk_result != VK_SUCCESS)
    {
        sh_end_temporary_memory(temp_memory);
        return layers_node;
    }

    for (uint32_t i = 0; i < layer_count; i += 1)
    {
        VkLayerProperties *layer = layers + i;

        Node *layer_node = push_object(allocator, layers_node, ShStringLiteral("__layer__"), true);

        push_string(allocator, layer_node, ShStringLiteral("name"), sh_copy_string(allocator, ShCString(layer->layerName)));
        if (layer->implementationVersion >= (1 << 12))
        {
            push_string(allocator, layer_node, ShStringLiteral("version"), vk_version_to_string(thread_context, allocator, layer->implementationVersion));
        }
        else
        {
            push_integer(allocator, layer_node, ShStringLiteral("version"), layer->implementationVersion);
        }
        push_string(allocator, layer_node, ShStringLiteral("spec_version"), vk_version_to_string(thread_context, allocator, layer->specVersion));
        push_string(allocator, layer_node, ShStringLiteral("description"), sh_copy_string(allocator, ShCString(layer->description)));
    }

    sh_end_temporary_memory(temp_memory);

    return layers_node;
}

static Node *
vulkan_command_extensions(ShThreadContext *thread_context, ShAllocator allocator, void *context, ShString command, Node *parent)
{
    VulkanContext *vulkan_context = (VulkanContext *) context;

    Node *extensions_node = push_array(allocator, parent, ShStringLiteral("extensions"), false);

    ShTemporaryMemory temp_memory = sh_begin_temporary_memory(thread_context, 1, &allocator);

    uint32_t extension_count = 0;

    VkResult vk_result;

    vk_result = vulkan_context->vkEnumerateInstanceExtensionProperties(NULL, &extension_count, NULL);

    if (vk_result != VK_SUCCESS)
    {
        sh_end_temporary_memory(temp_memory);
        return extensions_node;
    }

    VkExtensionProperties *extensions = sh_alloc_array(temp_memory.allocator, VkExtensionProperties, extension_count);

    vk_result = vulkan_context->vkEnumerateInstanceExtensionProperties(NULL, &extension_count, extensions);

    if (vk_result != VK_SUCCESS)
    {
        sh_end_temporary_memory(temp_memory);
        return extensions_node;
    }

    for (uint32_t i = 0; i < extension_count; i += 1)
    {
        VkExtensionProperties *extension = extensions + i;

        Node *extension_node = push_object(allocator, extensions_node, ShStringLiteral("__extension__"), true);

        push_string(allocator, extension_node, ShStringLiteral("name"), sh_copy_string(allocator, ShCString(extension->extensionName)));
        push_integer(allocator, extension_node, ShStringLiteral("version"), extension->specVersion);
    }

    sh_end_temporary_memory(temp_memory);

    return extensions_node;
}

static Node *
vulkan_command_devices(ShThreadContext *thread_context, ShAllocator allocator, void *context, ShString command, Node *parent)
{
    VulkanContext *vulkan_context = (VulkanContext *) context;

    Node *devices_node = push_array(allocator, parent, ShStringLiteral("devices"), false);

    ShTemporaryMemory temp_memory = sh_begin_temporary_memory(thread_context, 1, &allocator);

    uint32_t device_count = 0;

    VkResult vk_result;

    vk_result = vulkan_context->vkEnumeratePhysicalDevices(vulkan_context->instance, &device_count, NULL);

    if (vk_result != VK_SUCCESS)
    {
        sh_end_temporary_memory(temp_memory);
        return devices_node;
    }

    VkPhysicalDevice *devices = sh_alloc_array(temp_memory.allocator, VkPhysicalDevice, device_count);

    vk_result = vulkan_context->vkEnumeratePhysicalDevices(vulkan_context->instance, &device_count, devices);

    if (vk_result != VK_SUCCESS)
    {
        sh_end_temporary_memory(temp_memory);
        return devices_node;
    }

    for (uint32_t i = 0; i < device_count; i += 1)
    {
        ShTemporaryMemory inner_temp_memory = sh_begin_temporary_memory(thread_context, 1, &allocator);

        VkPhysicalDeviceProperties properties;
        vulkan_context->vkGetPhysicalDeviceProperties(devices[i], &properties);

        Node *device_node = push_object(allocator, devices_node, ShStringLiteral("__device__"), false);

        push_string(allocator, device_node, ShStringLiteral("name"), sh_copy_string(allocator, ShCString(properties.deviceName)));
        push_string(allocator, device_node, ShStringLiteral("type"), vk_physical_device_type_to_string(thread_context, allocator, properties.deviceType));
        push_string(allocator, device_node, ShStringLiteral("api_version"), vk_version_to_string(thread_context, allocator, properties.apiVersion));

        Node *extensions_node = push_array(allocator, device_node, ShStringLiteral("extensions"), false);

        uint32_t device_extension_count = 0;

        vk_result = vulkan_context->vkEnumerateDeviceExtensionProperties(devices[i], NULL, &device_extension_count, NULL);

        if (vk_result != VK_SUCCESS)
        {
            sh_end_temporary_memory(inner_temp_memory);
            continue;
        }

        VkExtensionProperties *device_extensions = sh_alloc_array(inner_temp_memory.allocator, VkExtensionProperties, device_extension_count);

        vk_result = vulkan_context->vkEnumerateDeviceExtensionProperties(devices[i], NULL, &device_extension_count, device_extensions);

        if (vk_result != VK_SUCCESS)
        {
            sh_end_temporary_memory(inner_temp_memory);
            continue;
        }

        for (uint32_t j = 0; j < device_extension_count; j += 1)
        {
            VkExtensionProperties *extension = device_extensions + j;

            Node *extension_node = push_object(allocator, extensions_node, ShStringLiteral("__extension__"), true);

            push_string(allocator, extension_node, ShStringLiteral("name"), sh_copy_string(allocator, ShCString(extension->extensionName)));
            push_integer(allocator, extension_node, ShStringLiteral("version"), extension->specVersion);
        }

        sh_end_temporary_memory(inner_temp_memory);
    }

    sh_end_temporary_memory(temp_memory);

    return devices_node;
}

static InfoCommand vulkan_commands[] = {
    { ShStringConstant("version")   , vulkan_command_version    },
    { ShStringConstant("layers")    , vulkan_command_layers     },
    { ShStringConstant("extensions"), vulkan_command_extensions },
    { ShStringConstant("devices")   , vulkan_command_devices    },
};

static Node *
info_command_vulkan(ShThreadContext *thread_context, ShAllocator allocator, void *context, ShString command, Node *parent)
{
    return handle_sub_command(thread_context, allocator, command, ShStringLiteral("vulkan"),
                              parent, ShArrayCount(vulkan_commands), vulkan_commands,
                              begin_vulkan_context, end_vulkan_context);
}

#endif // SH_PLATFORM_ANDROID || SH_PLATFORM_WINDOWS || SH_PLATFORM_LINUX

#if SH_PLATFORM_LINUX

#  include <wayland-client.h>

#  include "wayland/drm_fourcc.h"

#  include "wayland/linux-dmabuf-unstable-v1.h"
#  include "wayland/linux-dmabuf-unstable-v1.c"

typedef struct
{
    uint32_t format;
    uint64_t modifier;
} WaylandDmaBufFormat;

typedef struct
{
    ShString name;
    uint32_t version;
} WaylandInterface;

typedef struct
{
    ShAllocator allocator;

    struct wl_display *display;

    WaylandInterface *interfaces;
    WaylandDmaBufFormat *dmabuf_formats;
} WaylandContext;

static ShString
drm_format_to_string(ShThreadContext *thread_context, ShAllocator allocator, uint32_t format)
{
    ShString result = ShStringEmpty;

#define NAME(name) case name: result = ShStringLiteral(#name); break

    switch (format)
    {
        NAME(DRM_FORMAT_INVALID);
        NAME(DRM_FORMAT_C1);
        NAME(DRM_FORMAT_C2);
        NAME(DRM_FORMAT_C4);
        NAME(DRM_FORMAT_C8);
        NAME(DRM_FORMAT_D1);
        NAME(DRM_FORMAT_D2);
        NAME(DRM_FORMAT_D4);
        NAME(DRM_FORMAT_D8);
        NAME(DRM_FORMAT_R1);
        NAME(DRM_FORMAT_R2);
        NAME(DRM_FORMAT_R4);
        NAME(DRM_FORMAT_R8);
        NAME(DRM_FORMAT_R10);
        NAME(DRM_FORMAT_R12);
        NAME(DRM_FORMAT_R16);
        NAME(DRM_FORMAT_RG88);
        NAME(DRM_FORMAT_GR88);
        NAME(DRM_FORMAT_RG1616);
        NAME(DRM_FORMAT_GR1616);
        NAME(DRM_FORMAT_RGB332);
        NAME(DRM_FORMAT_BGR233);
        NAME(DRM_FORMAT_XRGB4444);
        NAME(DRM_FORMAT_XBGR4444);
        NAME(DRM_FORMAT_RGBX4444);
        NAME(DRM_FORMAT_BGRX4444);
        NAME(DRM_FORMAT_ARGB4444);
        NAME(DRM_FORMAT_ABGR4444);
        NAME(DRM_FORMAT_RGBA4444);
        NAME(DRM_FORMAT_BGRA4444);
        NAME(DRM_FORMAT_XRGB1555);
        NAME(DRM_FORMAT_XBGR1555);
        NAME(DRM_FORMAT_RGBX5551);
        NAME(DRM_FORMAT_BGRX5551);
        NAME(DRM_FORMAT_ARGB1555);
        NAME(DRM_FORMAT_ABGR1555);
        NAME(DRM_FORMAT_RGBA5551);
        NAME(DRM_FORMAT_BGRA5551);
        NAME(DRM_FORMAT_RGB565);
        NAME(DRM_FORMAT_BGR565);
        NAME(DRM_FORMAT_RGB888);
        NAME(DRM_FORMAT_BGR888);
        NAME(DRM_FORMAT_XRGB8888);
        NAME(DRM_FORMAT_XBGR8888);
        NAME(DRM_FORMAT_RGBX8888);
        NAME(DRM_FORMAT_BGRX8888);
        NAME(DRM_FORMAT_ARGB8888);
        NAME(DRM_FORMAT_ABGR8888);
        NAME(DRM_FORMAT_RGBA8888);
        NAME(DRM_FORMAT_BGRA8888);
        NAME(DRM_FORMAT_XRGB2101010);
        NAME(DRM_FORMAT_XBGR2101010);
        NAME(DRM_FORMAT_RGBX1010102);
        NAME(DRM_FORMAT_BGRX1010102);
        NAME(DRM_FORMAT_ARGB2101010);
        NAME(DRM_FORMAT_ABGR2101010);
        NAME(DRM_FORMAT_RGBA1010102);
        NAME(DRM_FORMAT_BGRA1010102);
        NAME(DRM_FORMAT_RGB161616);
        NAME(DRM_FORMAT_BGR161616);
        NAME(DRM_FORMAT_XRGB16161616);
        NAME(DRM_FORMAT_XBGR16161616);
        NAME(DRM_FORMAT_ARGB16161616);
        NAME(DRM_FORMAT_ABGR16161616);
        NAME(DRM_FORMAT_XRGB16161616F);
        NAME(DRM_FORMAT_XBGR16161616F);
        NAME(DRM_FORMAT_ARGB16161616F);
        NAME(DRM_FORMAT_ABGR16161616F);
        NAME(DRM_FORMAT_R16F);
        NAME(DRM_FORMAT_GR1616F);
        NAME(DRM_FORMAT_BGR161616F);
        NAME(DRM_FORMAT_R32F);
        NAME(DRM_FORMAT_GR3232F);
        NAME(DRM_FORMAT_BGR323232F);
        NAME(DRM_FORMAT_ABGR32323232F);
        NAME(DRM_FORMAT_AXBXGXRX106106106106);
        NAME(DRM_FORMAT_YUYV);
        NAME(DRM_FORMAT_YVYU);
        NAME(DRM_FORMAT_UYVY);
        NAME(DRM_FORMAT_VYUY);
        NAME(DRM_FORMAT_AYUV);
        NAME(DRM_FORMAT_AVUY8888);
        NAME(DRM_FORMAT_XYUV8888);
        NAME(DRM_FORMAT_XVUY8888);
        NAME(DRM_FORMAT_VUY888);
        NAME(DRM_FORMAT_VUY101010);
        NAME(DRM_FORMAT_Y210);
        NAME(DRM_FORMAT_Y212);
        NAME(DRM_FORMAT_Y216);
        NAME(DRM_FORMAT_Y410);
        NAME(DRM_FORMAT_Y412);
        NAME(DRM_FORMAT_Y416);
        NAME(DRM_FORMAT_XVYU2101010);
        NAME(DRM_FORMAT_XVYU12_16161616);
        NAME(DRM_FORMAT_XVYU16161616);
        NAME(DRM_FORMAT_Y0L0);
        NAME(DRM_FORMAT_X0L0);
        NAME(DRM_FORMAT_Y0L2);
        NAME(DRM_FORMAT_X0L2);
        NAME(DRM_FORMAT_YUV420_8BIT);
        NAME(DRM_FORMAT_YUV420_10BIT);
        NAME(DRM_FORMAT_XRGB8888_A8);
        NAME(DRM_FORMAT_XBGR8888_A8);
        NAME(DRM_FORMAT_RGBX8888_A8);
        NAME(DRM_FORMAT_BGRX8888_A8);
        NAME(DRM_FORMAT_RGB888_A8);
        NAME(DRM_FORMAT_BGR888_A8);
        NAME(DRM_FORMAT_RGB565_A8);
        NAME(DRM_FORMAT_BGR565_A8);
        NAME(DRM_FORMAT_NV12);
        NAME(DRM_FORMAT_NV21);
        NAME(DRM_FORMAT_NV16);
        NAME(DRM_FORMAT_NV61);
        NAME(DRM_FORMAT_NV24);
        NAME(DRM_FORMAT_NV42);
        NAME(DRM_FORMAT_NV15);
        NAME(DRM_FORMAT_NV20);
        NAME(DRM_FORMAT_NV30);
        NAME(DRM_FORMAT_P210);
        NAME(DRM_FORMAT_P010);
        NAME(DRM_FORMAT_P012);
        NAME(DRM_FORMAT_P016);
        NAME(DRM_FORMAT_P030);
        NAME(DRM_FORMAT_Q410);
        NAME(DRM_FORMAT_Q401);
        NAME(DRM_FORMAT_S010);
        NAME(DRM_FORMAT_S210);
        NAME(DRM_FORMAT_S410);
        NAME(DRM_FORMAT_S012);
        NAME(DRM_FORMAT_S212);
        NAME(DRM_FORMAT_S412);
        NAME(DRM_FORMAT_S016);
        NAME(DRM_FORMAT_S216);
        NAME(DRM_FORMAT_S416);
        NAME(DRM_FORMAT_YUV410);
        NAME(DRM_FORMAT_YVU410);
        NAME(DRM_FORMAT_YUV411);
        NAME(DRM_FORMAT_YVU411);
        NAME(DRM_FORMAT_YUV420);
        NAME(DRM_FORMAT_YVU420);
        NAME(DRM_FORMAT_YUV422);
        NAME(DRM_FORMAT_YVU422);
        NAME(DRM_FORMAT_YUV444);
        NAME(DRM_FORMAT_YVU444);

        default:
        {
            ShTemporaryMemory temp_memory = sh_begin_temporary_memory(thread_context, 1, &allocator);

            ShStringBuilder sb;
            sh_string_builder_init(&sb, temp_memory.allocator);

            sh_string_builder_append_string(&sb, ShStringLiteral("<unknown-drm-format: "));
            sh_string_builder_append_u8(&sb, (uint8_t) format);
            sh_string_builder_append_u8(&sb, (uint8_t) (format >>  8));
            sh_string_builder_append_u8(&sb, (uint8_t) (format >> 16));
            sh_string_builder_append_u8(&sb, (uint8_t) (format >> 24));
            sh_string_builder_append_string(&sb, ShStringLiteral(">"));

            result = sh_string_builder_to_string(&sb, allocator);

            sh_end_temporary_memory(temp_memory);
        } break;
    }

#undef NAME

    return result;
}

static ShString
drm_format_modifier_to_string(ShThreadContext *thread_context, ShAllocator allocator, uint64_t modifier)
{
    ShTemporaryMemory temp_memory = sh_begin_temporary_memory(thread_context, 1, &allocator);

    ShStringBuilder sb;
    sh_string_builder_init(&sb, temp_memory.allocator);

#define NAME(name) case name: sh_string_builder_append_unsigned_number(&sb, modifier, 16, '0', 16, true); sh_string_builder_append_string(&sb, ShStringLiteral(" = " #name)); break

    switch (modifier)
    {
        NAME(DRM_FORMAT_MOD_INVALID);
        NAME(DRM_FORMAT_MOD_LINEAR);

        NAME(I915_FORMAT_MOD_X_TILED);
        NAME(I915_FORMAT_MOD_Y_TILED);
        NAME(I915_FORMAT_MOD_Yf_TILED);
        NAME(I915_FORMAT_MOD_Y_TILED_CCS);
        NAME(I915_FORMAT_MOD_Yf_TILED_CCS);
        NAME(I915_FORMAT_MOD_Y_TILED_GEN12_RC_CCS);
        NAME(I915_FORMAT_MOD_Y_TILED_GEN12_MC_CCS);
        NAME(I915_FORMAT_MOD_Y_TILED_GEN12_RC_CCS_CC);
        NAME(I915_FORMAT_MOD_4_TILED);
        NAME(I915_FORMAT_MOD_4_TILED_DG2_RC_CCS);
        NAME(I915_FORMAT_MOD_4_TILED_DG2_MC_CCS);
        NAME(I915_FORMAT_MOD_4_TILED_DG2_RC_CCS_CC);
        NAME(I915_FORMAT_MOD_4_TILED_MTL_RC_CCS);
        NAME(I915_FORMAT_MOD_4_TILED_MTL_MC_CCS);
        NAME(I915_FORMAT_MOD_4_TILED_MTL_RC_CCS_CC);
        NAME(I915_FORMAT_MOD_4_TILED_LNL_CCS);
        NAME(I915_FORMAT_MOD_4_TILED_BMG_CCS);

        NAME(DRM_FORMAT_MOD_SAMSUNG_64_32_TILE);
        NAME(DRM_FORMAT_MOD_SAMSUNG_16_16_TILE);

        NAME(DRM_FORMAT_MOD_QCOM_COMPRESSED);
        NAME(DRM_FORMAT_MOD_QCOM_TILED3);
        NAME(DRM_FORMAT_MOD_QCOM_TILED2);
        NAME(DRM_FORMAT_MOD_QCOM_TIGHT);
        NAME(DRM_FORMAT_MOD_QCOM_TILE);
        NAME(DRM_FORMAT_MOD_QTI_SECURE);

        NAME(DRM_FORMAT_MOD_MTK_16L_32S_TILE);

        NAME(DRM_FORMAT_MOD_APPLE_GPU_TILED);
        NAME(DRM_FORMAT_MOD_APPLE_GPU_TILED_COMPRESSED);

        default:
        {
            sh_string_builder_append_unsigned_number(&sb, modifier, 16, '0', 16, true);
            sh_string_builder_append_string(&sb, ShStringLiteral(" = <unknown>"));
        } break;
    }

#undef NAME

    ShString result = sh_string_builder_to_string(&sb, allocator);

    sh_end_temporary_memory(temp_memory);

    return result;
}

static void
wayland_dmabuf_format(void *data, struct zwp_linux_dmabuf_v1 *dmabuf, uint32_t format)
{
    (void) data;
    (void) dmabuf;
    (void) format;
}

static void
wayland_dmabuf_modifier(void *data, struct zwp_linux_dmabuf_v1 *dmabuf, uint32_t format, uint32_t modifier_hi, uint32_t modifier_lo)
{
    WaylandContext *wayland_context = (WaylandContext *) data;

    WaylandDmaBufFormat *dmabuf_format = sh_array_append(wayland_context->dmabuf_formats);

    dmabuf_format->format   = format;
    dmabuf_format->modifier = ((uint64_t) modifier_hi << 32) | (uint64_t) modifier_lo;
}

static const struct zwp_linux_dmabuf_v1_listener wayland_dmabuf_listener = {
    .format   = wayland_dmabuf_format,
    .modifier = wayland_dmabuf_modifier,
};

static void
wayland_registry_global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version)
{
    WaylandContext *wayland_context = (WaylandContext *) data;

    ShString interface_name = ShCString(interface);

    WaylandInterface *interf = sh_array_append(wayland_context->interfaces);

    interf->name = sh_copy_string(wayland_context->allocator, interface_name);
    interf->version = version;

    if (sh_string_equal(interface_name, ShCString(zwp_linux_dmabuf_v1_interface.name)))
    {
        if (version >= 3)
        {
            struct zwp_linux_dmabuf_v1 *dmabuf = (struct zwp_linux_dmabuf_v1 *) wl_registry_bind(registry, name, &zwp_linux_dmabuf_v1_interface, 3);
            zwp_linux_dmabuf_v1_add_listener(dmabuf, &wayland_dmabuf_listener, wayland_context);
        }
    }
}

static void
wayland_registry_global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
}

static const struct wl_registry_listener wayland_registry_listener = {
    .global        = wayland_registry_global,
    .global_remove = wayland_registry_global_remove,
};

static void *
begin_wayland_context(ShAllocator allocator)
{
    WaylandContext *wayland_context = sh_alloc_type(allocator, WaylandContext);

    wayland_context->allocator = allocator;
    wayland_context->display = wl_display_connect(NULL);

    if (!wayland_context->display)
    {
        sh_free(allocator, wayland_context);
        return NULL;
    }

    wayland_context->interfaces = NULL;
    sh_array_init(wayland_context->interfaces, 16, allocator);

    wayland_context->dmabuf_formats = NULL;
    sh_array_init(wayland_context->dmabuf_formats, 16, allocator);

    struct wl_registry *registry = wl_display_get_registry(wayland_context->display);
    wl_registry_add_listener(registry, &wayland_registry_listener, wayland_context);

    wl_display_roundtrip(wayland_context->display);
    wl_display_roundtrip(wayland_context->display);

    return wayland_context;
}

static void
end_wayland_context(ShAllocator allocator, void *context)
{
    WaylandContext *wayland_context = (WaylandContext *) context;

    wl_display_disconnect(wayland_context->display);

    sh_array_free(wayland_context->interfaces);
    sh_free(allocator, wayland_context);
}

static Node *
wayland_command_interfaces(ShThreadContext *thread_context, ShAllocator allocator, void *context, ShString command, Node *parent)
{
    WaylandContext *wayland_context = (WaylandContext *) context;

    Node *interfaces_node = push_array(allocator, parent, ShStringLiteral("interfaces"), false);

    for (usize i = 0; i < sh_array_count(wayland_context->interfaces); i += 1)
    {
        WaylandInterface *interf = wayland_context->interfaces + i;

        Node *interface_node = push_object(allocator, interfaces_node, ShStringLiteral("__interface__"), true);

        push_string(allocator, interface_node, ShStringLiteral("name"), sh_copy_string(allocator, interf->name));
        push_integer(allocator, interface_node, ShStringLiteral("version"), interf->version);
    }
}

static Node *
wayland_command_dmabuf_formats(ShThreadContext *thread_context, ShAllocator allocator, void *context, ShString command, Node *parent)
{
    WaylandContext *wayland_context = (WaylandContext *) context;

    Node *dmabuf_formats_node = push_array(allocator, parent, ShStringLiteral("dmabuf_formats"), false);

    for (usize i = 0; i < sh_array_count(wayland_context->dmabuf_formats); i += 1)
    {
        WaylandDmaBufFormat *dmabuf_format = wayland_context->dmabuf_formats + i;

        Node *dmabuf_format_node = push_object(allocator, dmabuf_formats_node, ShStringLiteral("__dmabuf_format__"), true);

        push_string(allocator, dmabuf_format_node, ShStringLiteral("format"), drm_format_to_string(thread_context, allocator, dmabuf_format->format));
        push_string(allocator, dmabuf_format_node, ShStringLiteral("modifier"), drm_format_modifier_to_string(thread_context, allocator, dmabuf_format->modifier));
    }
}

static InfoCommand wayland_commands[] = {
    { ShStringConstant("interfaces")    , wayland_command_interfaces     },
    { ShStringConstant("dmabuf_formats"), wayland_command_dmabuf_formats },
};

static Node *
info_command_wayland(ShThreadContext *thread_context, ShAllocator allocator, void *context, ShString command, Node *parent)
{
    return handle_sub_command(thread_context, allocator, command, ShStringLiteral("wayland"),
                              parent, ShArrayCount(wayland_commands), wayland_commands,
                              begin_wayland_context, end_wayland_context);
}

#endif // SH_PLATFORM_LINUX

static InfoCommand info_commands[] = {
#if SH_PLATFORM_MACOS
    { ShStringConstant("metal") , info_command_metal  },
#endif
#if SH_PLATFORM_ANDROID || SH_PLATFORM_WINDOWS || SH_PLATFORM_LINUX
    { ShStringConstant("vulkan"), info_command_vulkan },
#endif
#if SH_PLATFORM_LINUX
    { ShStringConstant("wayland"), info_command_wayland },
#endif
};

static void
output_json(ShThreadContext *thread_context, Node *node, int32_t indentation, int32_t width_count, int32_t *widths)
{
    switch (node->type)
    {
        case NodeTypeObject:
        {
            if (node->compressed)
            {
                printf("{");

                int32_t index = 0;

                for (Node *n = node->first; n; n = n->next, index += 1)
                {
                    printf(" \"%" ShStringFmt "\": ", ShStringArg(n->name));

                    if (index < width_count)
                    {
                        int32_t width = 0;

                        switch (n->type)
                        {
                            case NodeTypeInteger: width = widths[index] - ((int32_t) n->name.count + (int32_t) snprintf(NULL, 0, "%" PRId64, n->value._integer64)); break;
                            case NodeTypeFloat:   width = widths[index] - ((int32_t) n->name.count + (int32_t) snprintf(NULL, 0, "%f", n->value._float64));         break;
                            default: break;
                        }

                        printf("%*s", width, "");
                    }

                    output_json(thread_context, n, indentation + 2, 0, NULL);

                    if (index < width_count)
                    {
                        int32_t width = 0;

                        switch (n->type)
                        {
                            case NodeTypeString:  width = widths[index] - ((int32_t) n->name.count + (int32_t) n->value._str.count + 2);                               break;
                            case NodeTypeBoolean: width = widths[index] - ((int32_t) n->name.count + (n->value._bool ? (sizeof("true") - 1) : (sizeof("false") - 1))); break;
                            default: break;
                        }

                        printf("%*s", width, "");
                    }

                    if (n->next)
                    {
                        printf(",");
                    }
                }

                printf(" }");
            }
            else
            {
                printf("{\n");

                for (Node *n = node->first; n; n = n->next)
                {
                    printf("%*s\"%" ShStringFmt "\": ", indentation + 2, "", ShStringArg(n->name));
                    output_json(thread_context, n, indentation + 2, 0, NULL);

                    if (n->next)
                    {
                        printf(",\n");
                    }
                    else
                    {
                        printf("\n");
                    }
                }

                printf("%*s}", indentation, "");
            }
        } break;

        case NodeTypeArray:
        {
            if (node->compressed)
            {
                printf("[ ");

                for (Node *n = node->first; n; n = n->next)
                {
                    output_json(thread_context, n, indentation + 2, 0, NULL);

                    if (n->next)
                    {
                        printf(", ");
                    }
                    else
                    {
                        printf(" ");
                    }
                }

                printf("]");
            }
            else
            {
                int32_t column_count = 0;
                int32_t *column_widths = NULL;

                ShTemporaryMemory temp_memory = sh_begin_temporary_memory(thread_context, 0, NULL);

                Node *first = node->first;

                if (first && (first->type == NodeTypeObject) && first->compressed)
                {
                    column_count = first->count;
                    column_widths = sh_alloc_array(temp_memory.allocator, int32_t, column_count);

                    for (int32_t i = 0; i < column_count; i += 1)
                    {
                        column_widths[i] = 0;
                    }

                    for (Node *n = node->first; n; n = n->next)
                    {
                        if ((n->type == NodeTypeObject) && n->compressed)
                        {
                            int32_t index = 0;

                            for (Node *child = n->first; child && (index < column_count); child = child->next, index += 1)
                            {
                                int32_t width = 0;

                                switch (child->type)
                                {
                                    case NodeTypeString:  width = (int32_t) child->name.count + (int32_t) child->value._str.count + 2;                               break;
                                    case NodeTypeInteger: width = (int32_t) child->name.count + (int32_t) snprintf(NULL, 0, "%" PRId64, child->value._integer64);    break;
                                    case NodeTypeFloat:   width = (int32_t) child->name.count + (int32_t) snprintf(NULL, 0, "%f", child->value._float64);            break;
                                    case NodeTypeBoolean: width = (int32_t) child->name.count + (child->value._bool ? (sizeof("true") - 1) : (sizeof("false") - 1)); break;
                                    default: break;
                                }

                                if (width > column_widths[index])
                                {
                                    column_widths[index] = width;
                                }
                            }
                        }
                    }
                }

                printf("[\n");

                for (Node *n = node->first; n; n = n->next)
                {
                    printf("%*s", indentation + 2, "");
                    output_json(thread_context, n, indentation + 2, column_count, column_widths);

                    if (n->next)
                    {
                        printf(",\n");
                    }
                    else
                    {
                        printf("\n");
                    }
                }

                printf("%*s]", indentation, "");

                sh_end_temporary_memory(temp_memory);
            }
        } break;

        case NodeTypeString:
        {
            printf("\"%" ShStringFmt "\"", ShStringArg(node->value._str));
        } break;

        case NodeTypeInteger:
        {
            printf("%" PRId64, node->value._integer64);
        } break;

        case NodeTypeFloat:
        {
            printf("%f", node->value._float64);
        } break;

        case NodeTypeBoolean:
        {
            printf(node->value._bool ? "true" : "false");
        } break;
    }
}

static void
print_help(const char *program_name)
{
    fprintf(stderr, "usage: %s [--help | -h] <command>\n", program_name);
}

int main(int argument_count, char **arguments)
{
    ShString command = ShStringEmpty;

    if (argument_count > 1)
    {
        command = ShCString(arguments[1]);
    }

    if (sh_string_equal(command, ShStringLiteral("--help")) ||
        sh_string_equal(command, ShStringLiteral("-h")))
    {
        print_help(arguments[0]);
        return 0;
    }

    OutputFormat output_format = OutputFormatJson;

    ShString saved = command;
    ShString format = sh_string_split_right_on_char(&command, '.');

    if (sh_string_equal(format, ShStringLiteral("json")))
    {
        output_format = OutputFormatJson;
    }
    else if (sh_string_equal(format, ShStringLiteral("yaml")))
    {
        output_format = OutputFormatYaml;
    }
    else
    {
        command = saved;
    }

    ShAllocator allocator;
    allocator.data = NULL;
    allocator.func = c_default_allocator_func;

    ShThreadContext *thread_context = sh_thread_context_create(allocator, ShMiB(1));

    Node *root_node = handle_sub_command(thread_context, allocator, command, ShStringLiteral("__root__"),
                                         NULL, ShArrayCount(info_commands), info_commands,
                                         NULL, NULL);

    if (!root_node)
    {
        sh_thread_context_destroy(thread_context);
        thread_context = NULL;
        return 0;
    }

    switch (output_format)
    {
        case OutputFormatJson:
        {
            output_json(thread_context, root_node, 0, 0, NULL);
        } break;

        case OutputFormatYaml:
        {
        } break;
    }

    sh_thread_context_destroy(thread_context);
    thread_context = NULL;

    return 0;
}
