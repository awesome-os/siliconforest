add_kext_bundle(PDDARTSurvey.kext INFO_PLIST Info.plist KERNEL_PRIVATE
    BUNDLE_IDENTIFIER org.puredarwin.driver.PDDARTSurvey BUNDLE_VERSION 0.1.0
    ANTIMAIN_FUNCTION pd_dart_survey_stop)
set_property(TARGET PDDARTSurvey.kext PROPERTY OUTPUT_NAME PDDARTSurvey)
target_sources(PDDARTSurvey.kext PRIVATE PDDARTSurvey.cpp board.c survey.c)
target_include_directories(PDDARTSurvey.kext PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(PDDARTSurvey.kext PRIVATE xnu_kernel_private_headers)
target_compile_options(PDDARTSurvey.kext PRIVATE -Wall -Wextra -Wframe-larger-than=1024)
set_property(TARGET PDDARTSurvey.kext PROPERTY C_STANDARD 11)
set_property(TARGET PDDARTSurvey.kext PROPERTY CXX_STANDARD 14)
install(TARGETS PDDARTSurvey.kext DESTINATION System/Library/Extensions COMPONENT BaseSystem)
