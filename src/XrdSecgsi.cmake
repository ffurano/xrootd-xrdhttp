
include( XRootDCommon )

#-------------------------------------------------------------------------------
# Shared library version
#-------------------------------------------------------------------------------
set( XRD_SEC_GSI_VERSION            0.0.1 )
set( XRD_SEC_GSI_SOVERSION          0 )

set( XRD_SEC_GSI_GMAPLDAP_VERSION   0.0.1 )
set( XRD_SEC_GSI_GMAPLDAP_SOVERSION 0 )

set( XRD_SEC_GSI_GMAPDN_VERSION     0.0.1 )
set( XRD_SEC_GSI_GMAPDN_SOVERSION   0 )

#-------------------------------------------------------------------------------
# The XrdSecgsi library
#-------------------------------------------------------------------------------
add_library(
  XrdSecgsi
  SHARED
  XrdSecgsi/XrdSecProtocolgsi.cc      XrdSecgsi/XrdSecProtocolgsi.hh
                                      XrdSecgsi/XrdSecgsiTrace.hh )

target_link_libraries(
  XrdSecgsi
  XrdCrypto
  XrdCryptossl
  XrdUtils )

set_target_properties(
  XrdSecgsi
  PROPERTIES
  VERSION   ${XRD_SEC_GSI_VERSION}
  SOVERSION ${XRD_SEC_GSI_SOVERSION} )

#-------------------------------------------------------------------------------
# The XrdSecgsiGMAPLDAP library
#-------------------------------------------------------------------------------
add_library(
  XrdSecgsiGMAPLDAP
  SHARED
  XrdSecgsi/XrdSecgsiGMAPFunLDAP.cc )

target_link_libraries(
  XrdSecgsiGMAPLDAP
  XrdSecgsi
  XrdUtils )

set_target_properties(
  XrdSecgsiGMAPLDAP
  PROPERTIES
  VERSION   ${XRD_SEC_GSI_GMAPLDAP_VERSION}
  SOVERSION ${XRD_SEC_GSI_GMAPLDAP_SOVERSION} )

#-------------------------------------------------------------------------------
# The XrdSecgsiAuthzVO library
#-------------------------------------------------------------------------------
add_library(
  XrdSecgsiAuthzVO
  SHARED
  XrdSecgsi/XrdSecgsiAuthzFunVO.cc )

target_link_libraries(
  XrdSecgsiAuthzVO
  XrdUtils )

set_target_properties(
  XrdSecgsiAuthzVO
  PROPERTIES
  VERSION   ${XRD_SEC_GSI_AUTHZVO_VERSION}
  SOVERSION ${XRD_SEC_GSI_AUTHZVO_SOVERSION} )

#-------------------------------------------------------------------------------
# The XrdSecgsiGMAPDN library
#-------------------------------------------------------------------------------
add_library(
  XrdSecgsiGMAPDN
  SHARED
  XrdSecgsi/XrdSecgsiGMAPFunDN.cc )

target_link_libraries(
  XrdSecgsiGMAPDN
  XrdSecgsi
  XrdUtils )

set_target_properties(
  XrdSecgsiGMAPDN
  PROPERTIES
  VERSION   ${XRD_SEC_GSI_GMAPDN_VERSION}
  SOVERSION ${XRD_SEC_GSI_GMAPDN_SOVERSION} )

#-------------------------------------------------------------------------------
# xrdgsiproxy
#-------------------------------------------------------------------------------
add_executable(
  xrdgsiproxy
  XrdSecgsi/XrdSecgsiProxy.cc )

target_link_libraries(
  xrdgsiproxy
  XrdUtils
  XrdCryptossl )

#-------------------------------------------------------------------------------
# Install
#-------------------------------------------------------------------------------
install(
  TARGETS XrdSecgsi XrdSecgsiGMAPDN XrdSecgsiGMAPLDAP xrdgsiproxy
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} )

install(
  FILES
  ${PROJECT_SOURCE_DIR}/docs/man/xrdgsiproxy.1
  DESTINATION ${CMAKE_INSTALL_MANDIR}/man1 )