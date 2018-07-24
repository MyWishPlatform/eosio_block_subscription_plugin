file(GLOB HEADERS "include/eosio/block_subscription_plugin/*.hpp")
add_library( block_subscription_plugin
             block_subscription_plugin.cpp
             ${HEADERS} )

target_link_libraries( block_subscription_plugin chain_plugin appbase fc )
target_include_directories( block_subscription_plugin PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include" )
