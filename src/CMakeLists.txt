# This file was automatically generated for projects
# without default 'CMakeLists.txt' file.

FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/*.c)

# 這個元件不叫 main，IDF 不會自動把所有元件列進相依，要自己列
idf_component_register(
    SRCS ${app_sources}
    INCLUDE_DIRS "."
    REQUIRES
        driver
        esp_lcd
        esp_timer
        nvs_flash
        # 語料放 storage 分區的 SPIFFS，內容在建置時燒進去
        spiffs
)

# 註：不使用 spiffs_create_partition_image()。那是 IDF 的 custom target，
# PlatformIO 的建置與 upload 流程不會執行它，image 不會產生也不會被燒進去。
# storage 分區的映像改由 scripts/gen-content.sh 產生、scripts/flash-content.sh 燒錄。
