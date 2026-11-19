idf_component_register(
    SRCS
        "csaw.c"
        "bus/message_bus.c"
        "wifi/wifi_manager.c"
        "channels/telegram/telegram_bot.c"
        "channels/feishu/feishu_bot.c"
        "channels/legacy_channels.c"
        "llm/llm_proxy.c"
        "llm/provider_chain.c"
        "agent/agent_loop.c"
        "agent/context_builder.c"
        "memory/memory_store.c"
        "memory/session_mgr.c"
        "storage/csaw_storage.c"
        "gateway/ws_server.c"
        "cli/serial_cli.c"
        "proxy/http_proxy.c"
        "cron/cron_service.c"
        "power/csaw_power.c"
        "heartbeat/heartbeat.c"
        "tools/tool_registry.c"
        "tools/tool_cron.c"
        "tools/tool_web_search.c"
        "tools/tool_get_time.c"
        "tools/tool_files.c"
        "tools/tool_gpio.c"
        "tools/gpio_policy.c"
        "skills/skill_loader.c"
        "onboard/wifi_onboard.c"
        "python/csaw_python_vm.c"
    INCLUDE_DIRS
        "."
        "${CMAKE_SOURCE_DIR}/components/csaw_micropython/include"
    REQUIRES
        csaw_hal csaw_ui csaw_micropython
        nvs_flash esp_wifi esp_netif esp_http_client esp_http_server
        esp_https_ota esp_event cjson spiffs console vfs app_update esp-tls
        esp_timer esp_websocket_client esp_driver_gpio
)
# The donor code uses strncpy(dst, src, sizeof(dst)-1) on zero-initialised
# structs throughout.  MicroPython's sdkconfig.base selects -O2, which makes
# GCC 15 promote these safe patterns (and bounded snprintf paths) to errors.
# Keep them as warnings rather than rewriting 55 call sites.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-error=stringop-truncation
    -Wno-error=format-truncation)
