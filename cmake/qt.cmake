# Look for QT, of any version.

set(PUTTY_QT_VERSION "ANY"
  CACHE STRING "Which major version of QT to build with")
set_property(CACHE PUTTY_QT_VERSION
  PROPERTY STRINGS ANY 6 5 4 NONE)


set(QT_DIR /home/akun/Qt5.12.9/5.12.9/gcc_64)
set(CMAKE_PREFIX_PATH $ENV{QT_DIR})

set(QT_VERSION Qt5)

#设置工程包含当前目录，非必须
set(CMAKE_INCLUDE_CURRENT_DIR ON)

#打开全局moc,设置自动生成moc文件，一定要设置
#set(CMAKE_AUTOMOC ON)
#打开全局uic，非必须
#set(CMAKE_AUTOUIC ON)
#打开全局rcc，非必须，如需打开，注意修改33行的qrc文件名
#set(CMAKE_AUTORCC ON)

#查找需要的Qt库文件，最好每一个库都要写，Qt也会根据依赖关系自动添加
find_package(Qt5 COMPONENTS  Widgets Core Gui X11Extras REQUIRED) 



message("-- Checking for QT5  ",${Qt5Core_INCLUDE_DIRS})
message("-- Checking for QT5  ",${Qt5Widgets_INCLUDE_DIRS})
message("-- Checking for QT5  ",${Qt5Gui_INCLUDE_DIRS})
set(QT_FOUND TRUE)

#查找当前文件夹中的所有源代码文件，也可以通过Set命令将所有文件设置为一个变量
#FILE(GLOB SRC_FILES "./*.cpp")
#查找设置当前文件夹中所有的头文件
#FILE(GLOB HEAD_FILES "./*.h")
#查找设置当前文件夹中所有的ui文件
#FILE(GLOB UI_FILES "./*.ui")

#通过Ui文件生成对应的头文件，一定要添加
#qt5_wrap_ui(WRAP_FILES ${UI_FILES})

#添加资源文件，非必须，一旦采用，注意修改相应的qrc文件名
#set(RCC_FILES rcc.qrc)

#将ui文件和生成文件整理在一个文件夹中，非必须
#source_group("Ui" FILES ${UI_FILES} ${WRAP_FILES} )

#创建工程文件
#add_executable(${PROJECT_NAME} ${SRC_FILES} ${HEAD_FILES} ${RCC_FILES} ${WRAP_FILES})

#添加Qt5依赖项
#target_link_libraries(${PROJECT_NAME} Qt5::Widgets Qt5::Core Qt5::Gui)