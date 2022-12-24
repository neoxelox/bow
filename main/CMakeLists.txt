idf_component_register(SRCS "main.cpp"
                       INCLUDE_DIRS ".")

target_compile_options(${COMPONENT_LIB} PRIVATE "-Wno-format")

fatfs_create_spiflash_image(static ../static FLASH_IN_PROJECT PRESERVE_TIME)
