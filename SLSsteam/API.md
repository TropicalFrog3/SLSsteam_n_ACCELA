SLSsteam has a toggleable API. If it's enabled you can write commands into /tmp/SLSsteam.API

API commands are split by using | as a seperator. So for example to install spacewar to library 0 you can write install|480|0 into SLSsteam.API

Arguments written in square brackets \[\] are optional

The current commands are:

### Application Manager

dumplibraries : Lists all available libraries' label, their path & their index \
install|appId|libraryIndex : Installs appId into library at libraryIndex \
uninstall|appId : Uninstalls appId

### Compatibility Manager

dumpcompat|appId : Lists all available compatibility tools for appId \
getcompat|appId : Lists the current compatibility tool for appId \
setcompat|appId\[|tool-name\] : Set's or clears the compatibility tool for appId
