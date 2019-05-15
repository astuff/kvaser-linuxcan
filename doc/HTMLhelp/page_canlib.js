var page_canlib =
[
    [ "Introduction", "page_user_guide_intro.html", [
      [ "Hello, CAN!", "page_user_guide_intro.html#section_user_guide_intro_hello", null ],
      [ "Error checking", "page_user_guide_intro.html#section_user_guide_canstatus", null ],
      [ "CANlib Core API Calls", "page_user_guide_intro.html#section_core_api_calls", null ]
    ] ],
    [ "Initialization", "page_user_guide_init.html", [
      [ "Library Initialization", "page_user_guide_init.html#section_user_guide_init_lib_init", null ],
      [ "Library Deinitialization and Cleanup", "page_user_guide_init.html#section_user_guide_init_lib_deinit", null ]
    ] ],
    [ "Devices and Channels", "page_user_guide_device_and_channel.html", [
      [ "Identifying Devices and Channels", "page_user_guide_device_and_channel.html#section_user_guide_identifying_devices", null ],
      [ "Channel Information", "page_user_guide_device_and_channel.html#section_user_guide_unique_device", null ],
      [ "Customized Channel Name", "page_user_guide_device_and_channel.html#section_user_guide_cust_channel_name", null ],
      [ "Virtual Channels", "page_user_guide_device_and_channel.html#section_user_guide_virtual", null ]
    ] ],
    [ "Open Channel", "page_user_guide_chips_channels.html", [
      [ "Open as CAN", "page_user_guide_chips_channels.html#section_user_guide_init_sel_channel_can", null ],
      [ "Open as CAN FD", "page_user_guide_chips_channels.html#section_user_guide_init_sel_channel_canfd", null ],
      [ "Close Channel", "page_user_guide_chips_channels.html#section_user_guide_init_sel_channel_close", null ],
      [ "Set CAN Bitrate", "page_user_guide_chips_channels.html#section_user_guide_init_bit_rate_can", null ],
      [ "Set CAN FD Bitrate", "page_user_guide_chips_channels.html#section_user_guide_init_bit_rate_canfd", null ],
      [ "CAN Driver Modes", "page_user_guide_chips_channels.html#section_user_guide_init_driver_modes", null ],
      [ "Code Sample", "page_user_guide_chips_channels.html#code_sample", null ]
    ] ],
    [ "Send and Receive CAN Messages", "page_user_guide_send_recv.html", [
      [ "Bus On / Bus Off", "page_user_guide_send_recv.html#section_user_guide_send_recv_bus_on_off", null ],
      [ "Reading Messages", "page_user_guide_send_recv.html#section_user_guide_send_recv_reading", null ],
      [ "Acceptance Filters", "page_user_guide_send_recv.html#section_user_guide_send_recv_filters", [
        [ "Code and Mask Format", "page_user_guide_send_recv.html#section_user_guide_misc_code_and_mask", null ]
      ] ],
      [ "Sending Messages", "page_user_guide_send_recv.html#section_user_guide_send_recv_sending", null ],
      [ "Object Buffers", "page_user_guide_send_recv.html#section_user_guide_send_recv_obj_buf", null ]
    ] ],
    [ "Bus Errors", "page_user_guide_bus_errors.html", [
      [ "Obtaining Bus Status Information", "page_user_guide_bus_errors.html#section_user_guide_dev_info_status", null ],
      [ "Overruns", "page_user_guide_bus_errors.html#section_user_guide_send_recv_overruns", null ],
      [ "Error Frames", "page_user_guide_bus_errors.html#section_user_guide_bus_errors_error_frames", null ],
      [ "SJA1000 Error Codes", "page_user_guide_bus_errors.html#section_user_guide_bus_errors_sja1000_error_codes", null ]
    ] ],
    [ "Time Measurement", "page_user_guide_time.html", [
      [ "Accuracy", "page_user_guide_time.html#section_user_guide_time_accuracy_and_resolution_accuracy", null ],
      [ "Resolution", "page_user_guide_time.html#section_user_guide_time_accuracy_and_resolution_resolution", null ],
      [ "Time Domain", "page_user_guide_time.html#section_user_guide_time_domain", null ]
    ] ],
    [ "Version Checking", "page_user_guide_version.html", null ],
    [ "Using Threads", "page_user_guide_threads.html", [
      [ "Threaded Applications", "page_user_guide_threads.html#section_user_guide_threads_applications", null ]
    ] ],
    [ "Asynchronous Notification", "page_user_guide_send_recv_asynch_not.html", [
      [ "Asynchronous Notifications", "page_user_guide_send_recv_asynch_not.html#section_user_guide_send_recv_asynch", [
        [ "Receive Events", "page_user_guide_send_recv_asynch_not.html#section_user_guide_send_recv_asynch_receive", null ],
        [ "Transmit Events", "page_user_guide_send_recv_asynch_not.html#section_user_guide_send_recv_asynch_transmit", null ],
        [ "Status Events", "page_user_guide_send_recv_asynch_not.html#section_user_guide_send_recv_asynch_status", null ],
        [ "Error Events", "page_user_guide_send_recv_asynch_not.html#section_user_guide_send_recv_asynch_error", null ]
      ] ],
      [ "Receiving Using Callback Function", "page_user_guide_send_recv_asynch_not.html#section_user_guide_send_recv_asynch_callback", null ]
    ] ],
    [ "t Programming", "page_user_guide_kvscript.html", [
      [ "Overview", "page_user_guide_kvscript.html#section_user_guide_kvscript_overview", null ],
      [ "Load and Unload Script", "page_user_guide_kvscript.html#section_user_guide_kvscript_loading", null ],
      [ "Start and Stop script", "page_user_guide_kvscript.html#section_user_guide_kvscript_start_stop", null ],
      [ "Environment Variable", "page_user_guide_kvscript.html#section_user_guide_kvscript_envvar", null ],
      [ "Send Event", "page_user_guide_kvscript.html#section_user_guide_kvscript_send_event", null ]
    ] ],
    [ "I/O Pin Handling", "page_user_guide_kviopin.html", [
      [ "Initialize", "page_user_guide_kviopin.html#section_user_guide_kviopin_init", null ],
      [ "Pin information", "page_user_guide_kviopin.html#section_user_guide_kviopin_info", null ],
      [ "IO pin types", "page_user_guide_kviopin.html#section_user_guide_kviopin_type", [
        [ "Analog Pins", "page_user_guide_kviopin.html#section_user_guide_kviopin_type_analog", null ],
        [ "Digital Pins", "page_user_guide_kviopin.html#section_user_guide_kviopin_type_digital", null ],
        [ "Relay Pins", "page_user_guide_kviopin.html#section_user_guide_kviopin_type_relay", null ]
      ] ]
    ] ],
    [ "Message Mailboxes", "page_user_guide_send_recv_mailboxes.html", [
      [ "Message Queue and Buffer Sizes", "page_user_guide_send_recv_mailboxes.html#section_user_guide_send_recv_queue_and_buf_sizes", null ],
      [ "Different CAN Frame Types", "page_user_guide_send_recv_mailboxes.html#section_user_guide_send_recv_sending_different_types", null ]
    ] ],
    [ "User Data in Kvaser Devices", "page_user_guide_userdata.html", [
      [ "Trying out the concept", "page_user_guide_userdata.html#section_user_guide_userdata_trying", null ],
      [ "Writing user data", "page_user_guide_userdata.html#section_example_c_customerdata_writing", null ],
      [ "Obtaining your own password", "page_user_guide_userdata.html#section_example_c_customerdata_obtaining", null ],
      [ "Reading User Data", "page_user_guide_userdata.html#section_user_guide_userdata_reading", [
        [ "Program to read User Data", "page_user_guide_userdata.html#section_example_c_read_customerdata", null ]
      ] ]
    ] ],
    [ "Windows Advanced Topics", "page_user_guide_install.html", [
      [ "Windows Installation troubleshooting", "page_user_guide_install.html#section_user_guide_build_installation_verification_3", null ],
      [ "Running the installation in silent mode", "page_user_guide_install.html#section_user_guide_build_installation_advanced_2", null ],
      [ "Custom Driver Install", "page_user_guide_install.html#section_user_guide_build_installation_custom", null ],
      [ "Using Debug DLLs", "page_user_guide_install.html#section_user_guide_build_installation_debug", null ]
    ] ],
    [ "Compiling and Compatibility", "page_user_guide_build.html", [
      [ "Compatibility", "page_user_guide_build.html#section_porting_code_older", null ],
      [ "Compiling and Linking Your Code", "page_user_guide_build.html#section_user_guide_build_compiling_linking", null ],
      [ "Deploying Your Application", "page_user_guide_build.html#section_user_guide_build_installation_deploy", null ],
      [ "Redistributable Files in CANlib SDK", "page_user_guide_build.html#section_user_guide_intro_redistributable", null ]
    ] ],
    [ "CANlib API Calls Grouped by Function", "page_canlib_api_calls_grouped_by_function.html", [
      [ "Information Services", "page_canlib_api_calls_grouped_by_function.html#section_information", null ],
      [ "Channel Open and Close", "page_canlib_api_calls_grouped_by_function.html#section_open", null ],
      [ "Channel Parameters", "page_canlib_api_calls_grouped_by_function.html#section_parameters", null ],
      [ "Receiving Messages", "page_canlib_api_calls_grouped_by_function.html#section_receiving", null ],
      [ "Sending Messages", "page_canlib_api_calls_grouped_by_function.html#section_sending", null ],
      [ "Notification and Waiting", "page_canlib_api_calls_grouped_by_function.html#section_notification", null ],
      [ "Object Buffers", "page_canlib_api_calls_grouped_by_function.html#section_object", null ],
      [ "Miscellaneous", "page_canlib_api_calls_grouped_by_function.html#section_miscellaneous", null ],
      [ "Time Domain Handling", "page_canlib_api_calls_grouped_by_function.html#section_time", null ]
    ] ],
    [ "Sample Programs (CANlib)", "page_user_guide_canlib_samples.html", "page_user_guide_canlib_samples" ]
];