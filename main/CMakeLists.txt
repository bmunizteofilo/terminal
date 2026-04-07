add_executable(terminal_BMT
    app/main.c
    app/terminal.c
    can/can_slcan.c
    dbc/dbc_parser.c
    ui/inverter_dashboard.c
    ui/serial_monitor.c
    ui/vcu_dashboard.c
    ui/web_ui.c
)

target_include_directories(terminal_BMT PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/app
    ${CMAKE_CURRENT_SOURCE_DIR}/can
    ${CMAKE_CURRENT_SOURCE_DIR}/dbc
    ${CMAKE_CURRENT_SOURCE_DIR}/ui
)

target_link_libraries(terminal_BMT PRIVATE ws2_32 shell32)
