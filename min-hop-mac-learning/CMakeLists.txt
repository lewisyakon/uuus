# 定义模块源文件



set(source_files
    model/min-hop-bridge-net-device.cc
    model/hop-count-client.cc
    model/hop-count-header.cc
    model/pause-header.cc
    model/three-phase-protocol.cc
    model/3PHeader.cc
    model/my_sendflowclient.cc
    model/PppRoundRobinQueue.cc
    model/FlowTagHeader.cc
)
set(header_files
    model/min-hop-bridge-net-device.h
    model/hop-count-client.h
    model/hop-count-header.h
    model/pause-header.h
    model/three-phase-protocol.h
    model/3PHeader.h
    model/my_sendflowclient.h
    model/PppRoundRobinQueue.h
    model/FlowTagHeader.h
)

set(libstats
    network
    core
    bridge
    internet
    applications
)

build_lib(
  LIBNAME min-hop-mac-learning
  SOURCE_FILES ${source_files}
  HEADER_FILES ${header_files}
  LIBRARIES_TO_LINK ${libstats}
)