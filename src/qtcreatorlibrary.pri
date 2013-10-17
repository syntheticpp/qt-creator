include($$replace(_PRO_FILE_PWD_, ([^/]+$), \\1/\\1_dependencies.pri))
TARGET = $$QTC_LIB_NAME

include(../qtcreator.pri)

# use precompiled header for libraries by default
isEmpty(vcproj) {
    isEmpty(PRECOMPILED_HEADER):PRECOMPILED_HEADER = $$PWD/shared/qtcreator_pch.h
} else {
    PCH = $$PWD/shared/qtcreator_pch_$${TARGET}.h
    !exists($$PCH) { 
        copyargs = $$PWD/shared/qtcreator_pch.h $$PCH
        copyargs = $$replace(copyargs, /, \\)
        message($$QMAKE_COPY $$copyargs)
        system($$QMAKE_COPY $$copyargs)
    }
    isEmpty(PRECOMPILED_HEADER):PRECOMPILED_HEADER = $$PCH
}

win32 {
    DLLDESTDIR = $$IDE_APP_PATH
}

DESTDIR = $$IDE_LIBRARY_PATH

include(rpath.pri)

TARGET = $$qtLibraryName($$TARGET)

TEMPLATE = lib
CONFIG += shared dll

contains(QT_CONFIG, reduce_exports):CONFIG += hide_symbols

!macx {
    win32 {
        dlltarget.path = $$QTC_PREFIX/bin
        INSTALLS += dlltarget
    } else {
        target.path = $$QTC_PREFIX/$$IDE_LIBRARY_BASENAME/qtcreator
        INSTALLS += target
    }
}
