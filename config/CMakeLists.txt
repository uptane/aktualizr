if(BUILD_DEB)
    install(FILES systemd/aktualizr-ubuntu.service
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/systemd/system
        RENAME aktualizr.service
        PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ
        COMPONENT aktualizr)

    install(FILES sota-local.toml
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/sota/conf.d
        PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ
        COMPONENT aktualizr)

    install(FILES secondary/virtualsec.json
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/sota/secondaries
        RENAME demo_secondary.json
        PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ
        COMPONENT aktualizr)
endif(BUILD_DEB)

install(DIRECTORY DESTINATION ${CMAKE_INSTALL_LIBDIR}/sota/conf.d COMPONENT aktualizr)
