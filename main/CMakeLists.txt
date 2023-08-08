idf_component_register(SRCS 
                    "main.c" 
                    "wifi.c"
                    "led_strip_encoder.c"
                    "led_strip_ctl.c"
                    "webserver.c"
                    "mount.c"
                    INCLUDE_DIRS "."
                    EMBED_FILES root.html)
