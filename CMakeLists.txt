cmake_minimum_required(VERSION 3.16)

# ---------------------------------------------------------------------------
# Double cible : desktop (par défaut) ou PS Vita (-DBUILD_VITA=ON)
# Le toolchain Vita doit être défini AVANT project().
# ---------------------------------------------------------------------------
option(BUILD_VITA "Compiler pour PS Vita avec VitaSDK" OFF)

if(BUILD_VITA)
  if(NOT DEFINED ENV{VITASDK})
    message(FATAL_ERROR "VITASDK n'est pas défini. Utilise scripts/build-vita.sh (Docker) ou installe VitaSDK.")
  endif()
  set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
endif()

project(vitagame C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.c)
# Code tiers (stb_vorbis, stb_image, h264bsd) : on ne corrige pas ses warnings
file(GLOB H264BSD_SOURCES src/third_party/h264bsd/*.c)
set_source_files_properties(src/third_party/stb_vorbis.c src/third_party/stb_image_impl.c src/third_party/pl_mpeg_impl.c
                            src/third_party/miniz.c ${H264BSD_SOURCES} PROPERTIES COMPILE_FLAGS "-w")
if(BUILD_VITA)  # la Vita décode le H.264 par son décodeur matériel (src/video_h264_vita.c) : h264bsd n'y sert à rien
  list(REMOVE_ITEM SOURCES ${H264BSD_SOURCES})
endif()
# miniz (lecture des zips de maps) : ni écriture d'archives, ni dates de fichiers (utime absent sur Vita)
add_compile_definitions(MINIZ_NO_ARCHIVE_WRITING_APIS MINIZ_NO_TIME MINIZ_NO_ZLIB_COMPATIBLE_NAMES)

# ---------------------------------------------------------------------------
# Identité du jeu : SEUL endroit où le nom, la version, le TITLEID et le dossier de sauvegarde sont écrits.
# Le code les reçoit en macros (GAME_NAME, GAME_VERSION, GAME_SAVE_SLUG) ; les scripts lisent ces lignes.
# TITLEID = 4 lettres majuscules + 5 chiffres, unique ; le changer impose de réinstaller le .vpk.
# ---------------------------------------------------------------------------
set(GAME_NAME      "Dancing Every Night")
set(GAME_VERSION   "1.0.0")
set(GAME_SAVE_SLUG "dancingeverynight")
set(VITA_TITLEID   "DNEN00001")

# Version du .vpk au format XX.YY, déduite de GAME_VERSION (majeure.mineure)
string(REPLACE "." ";" _game_version_parts "${GAME_VERSION}")
list(GET _game_version_parts 0 _game_version_major)
list(GET _game_version_parts 1 _game_version_minor)
string(LENGTH "${_game_version_major}" _len)
if(_len LESS 2)
  set(_game_version_major "0${_game_version_major}")
endif()
string(LENGTH "${_game_version_minor}" _len)
if(_len LESS 2)
  set(_game_version_minor "0${_game_version_minor}")
endif()
set(VITA_APP_NAME "${GAME_NAME}")
set(VITA_VERSION  "${_game_version_major}.${_game_version_minor}")

add_compile_definitions(GAME_NAME="${GAME_NAME}" GAME_VERSION="${GAME_VERSION}" GAME_SAVE_SLUG="${GAME_SAVE_SLUG}")

# Dossier d'assets embarqué dans le .vpk : assets/ en dev ; scripts/build-vita.sh (RELEASE=1) passe une copie
# limitée aux fichiers suivis par git, sans les morceaux personnels.
set(GAME_ASSETS_DIR "assets" CACHE STRING "dossier d'assets embarqué dans le .vpk (relatif à la source)")

if(VITA)
  include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)

  add_executable(${PROJECT_NAME} ${SOURCES})
  # Sections séparées + gc : retire le code inutilisé (stb_vorbis, SDL), binaire plus petit
  target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -O2 -ffunction-sections -fdata-sections)
  target_link_options(${PROJECT_NAME} PRIVATE -Wl,--gc-sections)

  # Remplissage optionnel (posé par scripts/build-vita.sh) contre le chevauchement de segments
  # de vita-elf-create ; main.c le référence (VITA_PAD_LINK) pour que --gc-sections le garde.
  set(VITA_PAD_BYTES 0 CACHE STRING "octets de remplissage ajoutés au binaire Vita")
  if(VITA_PAD_BYTES GREATER 0)
    file(WRITE ${CMAKE_BINARY_DIR}/vita_pad.c
         "const unsigned char vita_pad[${VITA_PAD_BYTES}] __attribute__((used)) = {1};\n")
    target_sources(${PROJECT_NAME} PRIVATE ${CMAKE_BINARY_DIR}/vita_pad.c)
    target_compile_definitions(${PROJECT_NAME} PRIVATE VITA_PAD_LINK=1)
  endif()

  # SDL2 fourni par VitaSDK (vdpm sdl2). Selon la version, la cible CMake
  # s'appelle SDL2::SDL2-static ou SDL2::SDL2 ; sinon on liste les libs à la main.
  find_package(SDL2 CONFIG QUIET)
  if(TARGET SDL2::SDL2-static)
    target_link_libraries(${PROJECT_NAME} SDL2::SDL2-static)
  elseif(TARGET SDL2::SDL2)
    target_link_libraries(${PROJECT_NAME} SDL2::SDL2)
  else()
    target_link_libraries(${PROJECT_NAME}
      SDL2
      SceDisplay_stub SceCtrl_stub SceAudio_stub SceAudioIn_stub
      SceSysmodule_stub SceGxm_stub SceCommonDialog_stub SceAppMgr_stub
      SceTouch_stub SceHid_stub SceMotion_stub ScePower_stub SceIme_stub
      m)
  endif()

  # Annonce des mises à jour (src/update.c) : libcurl + OpenSSL (VitaSDK) pour le HTTPS actuel ; quelle que soit la façon dont SDL2 a été trouvée
  target_link_libraries(${PROJECT_NAME} curl ssl crypto zstd z SceNet_stub SceNetCtl_stub SceSysmodule_stub)
  # Vidéos H.264 : décodeur matériel (src/video_h264_vita.c)
  target_link_libraries(${PROJECT_NAME} SceVideodec_stub)

  # SDL2 récentes de VitaSDK (image Docker de la CI) : rendu par vitaGL, écrit en C++, dont l'interface CMake oublie
  # ces bibliothèques. Sans effet sur une SDL2 qui ne s'en sert pas (archives statiques non référencées).
  # Groupe de l'éditeur de liens en fin de ligne, chemins complets : ni réordonné ni dédoublonné par CMake.
  set(_vita_lib "$ENV{VITASDK}/arm-vita-eabi/lib")
  if(EXISTS "${_vita_lib}/libvitaGL.a")
    target_link_libraries(${PROJECT_NAME} "-Wl,--start-group ${_vita_lib}/libvitaGL.a ${_vita_lib}/libvitashark.a \
${_vita_lib}/libSceShaccCgExt.a ${_vita_lib}/libSceShaccCg_stub.a ${_vita_lib}/libtaihen_stub.a \
${_vita_lib}/libSceKernelDmacMgr_stub.a ${_vita_lib}/libmathneon.a ${_vita_lib}/libstdc++.a ${_vita_lib}/libsupc++.a \
-lm -lc -Wl,--end-group")
  endif()

  vita_create_self(eboot.bin ${PROJECT_NAME} UNSAFE)

  vita_create_vpk(${PROJECT_NAME}.vpk ${VITA_TITLEID} eboot.bin
    VERSION ${VITA_VERSION}
    NAME    ${VITA_APP_NAME}
    FILE sce_sys/icon0.png                      sce_sys/icon0.png
    FILE sce_sys/livearea/contents/bg.png       sce_sys/livearea/contents/bg.png
    FILE sce_sys/livearea/contents/startup.png  sce_sys/livearea/contents/startup.png
    FILE sce_sys/livearea/contents/template.xml sce_sys/livearea/contents/template.xml
    FILE ${GAME_ASSETS_DIR} assets
  )
else()
  # --------------------------- Build desktop ---------------------------
  find_package(SDL2 REQUIRED)

  add_executable(${PROJECT_NAME} ${SOURCES})
  target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -g)

  # Builds distribués (scripts/build-pc.sh, CI) : SDL2 compilée dans le programme, rien à installer pour le joueur
  option(GAME_STATIC_SDL "PC : lier SDL2 en statique" OFF)

  if(TARGET SDL2::SDL2main)
    target_link_libraries(${PROJECT_NAME} SDL2::SDL2main)
  endif()
  if(GAME_STATIC_SDL)
    if(NOT TARGET SDL2::SDL2-static)
      message(FATAL_ERROR "GAME_STATIC_SDL : SDL2 statique introuvable (CMAKE_PREFIX_PATH vers une SDL2 compilée avec SDL_STATIC=ON)")
    endif()
    target_link_libraries(${PROJECT_NAME} SDL2::SDL2-static)
    if(WIN32)   # double-clic sans console ; libgcc et pthread dans l'exe (pas de DLL MinGW à fournir)
      set_target_properties(${PROJECT_NAME} PROPERTIES WIN32_EXECUTABLE TRUE)
      target_link_options(${PROJECT_NAME} PRIVATE -static)
    endif()
  elseif(TARGET SDL2::SDL2)
    target_link_libraries(${PROJECT_NAME} SDL2::SDL2)
  else()
    target_include_directories(${PROJECT_NAME} PRIVATE ${SDL2_INCLUDE_DIRS})
    target_link_libraries(${PROJECT_NAME} ${SDL2_LIBRARIES})
  endif()
  target_link_libraries(${PROJECT_NAME} m)

  # Bibliothèque en ligne (src/net.c) : WinHTTP sur Windows (aucune dépendance) ; libcurl du système sur macOS ; sur Linux,
  # libcurl est chargée à l'exécution (dlopen) : seuls ses en-têtes servent à la compilation, le jeu démarre sans elle.
  # Sans le nécessaire, le jeu compile sans réseau et la bibliothèque le dit.
  option(GAME_NET "PC : accès réseau de la bibliothèque en ligne" ON)
  if(GAME_NET)
    if(WIN32)
      target_compile_definitions(${PROJECT_NAME} PRIVATE HAVE_WINHTTP=1)
      target_link_libraries(${PROJECT_NAME} winhttp)
    else()
      find_package(CURL QUIET)
      if(CURL_FOUND)
        target_compile_definitions(${PROJECT_NAME} PRIVATE HAVE_CURL=1)
        if(APPLE)
          target_link_libraries(${PROJECT_NAME} CURL::libcurl)
        else()
          target_compile_definitions(${PROJECT_NAME} PRIVATE NET_DLOPEN=1)
          target_include_directories(${PROJECT_NAME} PRIVATE ${CURL_INCLUDE_DIRS})
          target_link_libraries(${PROJECT_NAME} ${CMAKE_DL_LIBS})
        endif()
      else()
        message(STATUS "libcurl introuvable : compilation sans réseau (la bibliothèque en ligne sera indisponible)")
      endif()
    endif()
  endif()

  # Copie des assets à côté de l'exécutable (équivalent de app0: sur Vita) ; les builds distribués
  # (scripts/build-pc.sh) prennent eux-mêmes les seuls assets commités
  option(GAME_COPY_ASSETS "PC : copier assets/ à côté de l'exécutable" ON)
  if(GAME_COPY_ASSETS)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_directory
              ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:${PROJECT_NAME}>/assets)
  endif()
endif()
