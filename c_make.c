#define C_MAKE_IMPLEMENTATION
#include "src/c_make.h"

C_MAKE_ENTRY()
{
    switch (c_make_target)
    {
        case TargetSetup:
        {
#if PLATFORM_WINDOWS
            ConfigValue vulkan_sdk_root_path = config_get("vulkan_sdk_root_path");

            if (!vulkan_sdk_root_path.is_valid ||
                (string_trim(CString(vulkan_sdk_root_path.val)).count == 0))
            {
                SoftwarePackage vulkan_sdk;

                if (c_make_find_best_software_package("C:\\VulkanSDK", StringLiteral(""), &vulkan_sdk))
                {
                    config_set("vulkan_sdk_root_path", vulkan_sdk.root_path);
                }
            }
#endif
        } break;

        case TargetBuild:
        {
            const char *target_c_compiler = get_target_c_compiler();

            Command cmd = { 0 };

            command_append(&cmd, target_c_compiler);
            command_append_command_line(&cmd, get_target_c_flags());
            command_append_default_compiler_flags(&cmd, get_build_type());

            if (get_target_platform() == PlatformWindows)
            {
                ConfigValue vulkan_sdk_root_path = config_get("vulkan_sdk_root_path");

                if (vulkan_sdk_root_path.is_valid &&
                    (string_trim(CString(vulkan_sdk_root_path.val)).count > 0))
                {
                    command_append(&cmd, c_string_concat("-I", c_string_path_concat(vulkan_sdk_root_path.val, "Include")));
                }
            }

            if (get_target_platform() == PlatformMacOs)
            {
                command_append(&cmd, "-ObjC");
            }

            command_append_output_executable(&cmd, c_string_path_concat(get_build_path(), "system_info"), get_target_platform());
            command_append(&cmd, c_string_path_concat(get_source_path(), "src", "system_info.c"));
            command_append_default_linker_flags(&cmd, get_target_architecture());

            if (get_target_platform() == CMakePlatformMacOs)
            {
                command_append(&cmd, "-framework", "Foundation", "-framework", "Metal");
            }
            else if (get_target_platform() == CMakePlatformLinux)
            {
                command_append(&cmd, "-lwayland-client");
            }

            c_make_log(LogLevelInfo, "compile 'system_info'\n");
            command_run_and_reset(&cmd);

            command_append(&cmd, target_c_compiler);
            command_append_command_line(&cmd, get_target_c_flags());
            command_append_default_compiler_flags(&cmd, get_build_type());

            command_append_output_executable(&cmd, c_string_path_concat(get_build_path(), "bdf2h"), get_target_platform());
            command_append(&cmd, c_string_path_concat(get_source_path(), "src", "bdf2h.c"));
            command_append_default_linker_flags(&cmd, get_target_architecture());

            c_make_log(LogLevelInfo, "compile 'bdf2h'\n");
            command_run_and_reset(&cmd);
        } break;

        case TargetInstall:
        {
        } break;
    }
}
