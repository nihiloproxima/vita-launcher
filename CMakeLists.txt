cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
  else()
    message(FATAL_ERROR "Please define VITASDK to point to your SDK path!")
  endif()
endif()

project(launcher C)
include("${VITASDK}/share/vita.cmake" REQUIRED)

set(VITA_APP_NAME "Launcher")
set(VITA_TITLEID  "LNCH00001")
set(VITA_VERSION  "01.00")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -O2")
set(VITA_MKSFOEX_FLAGS "${VITA_MKSFOEX_FLAGS} -d PARENTAL_LEVEL=1")

add_executable(${PROJECT_NAME}
  src/main.c src/apps.c src/covers.c src/idlist.c src/input.c src/panel.c
  src/sfo.c src/sheet.c src/sys.c src/ui.c
)
target_compile_definitions(${PROJECT_NAME} PRIVATE SELF_TITLEID="${VITA_TITLEID}")

target_link_libraries(${PROJECT_NAME}
  vita2d freetype png jpeg
  curl mbedtls mbedx509 mbedcrypto zstd z bz2 m pthread
  SceDisplay_stub SceGxm_stub SceSysmodule_stub SceCommonDialog_stub
  SceCtrl_stub SceTouch_stub SceAppMgr_stub SceAppUtil_stub
  ScePower_stub SceRtc_stub ScePgf_stub ScePvf_stub
  SceAVConfig_stub SceNet_stub SceNetCtl_stub ScePromoterUtil_stub
)

vita_create_self(${PROJECT_NAME}.self ${PROJECT_NAME})
vita_create_vpk(${PROJECT_NAME}.vpk ${VITA_TITLEID} ${PROJECT_NAME}.self
  VERSION ${VITA_VERSION}
  NAME ${VITA_APP_NAME}
  FILE sce_sys/icon0.png sce_sys/icon0.png
  FILE sce_sys/livearea/contents/bg.png sce_sys/livearea/contents/bg.png
  FILE sce_sys/livearea/contents/startup.png sce_sys/livearea/contents/startup.png
  FILE sce_sys/livearea/contents/template.xml sce_sys/livearea/contents/template.xml
  FILE fonts/Inter-Regular.ttf fonts/Inter-Regular.ttf
  FILE fonts/Inter-SemiBold.ttf fonts/Inter-SemiBold.ttf
  FILE fonts/OFL.txt fonts/OFL.txt
  FILE certs/cacert.pem certs/cacert.pem
)
