name    urlmon
type    win32

import	ole32.dll

1 stub CDLGetLongPathNameA
2 stub CDLGetLongPathNameW
@ stub AsyncGetClassBits
@ stub AsyncInstallDistributionUnit
@ stub BindAsyncMoniker
@ stub CoGetClassObjectFromURL
@ stub CoInstall
@ stub CoInternetCombineUrl
@ stub CoInternetCompareUrl
@ stub CoInternetCreateSecurityManager
@ stub CoInternetCreateZoneManager
@ stub CoInternetGetProtocolFlags
@ stub CoInternetGetSecurityUrl
@ stub CoInternetGetSession
@ stub CoInternetParseUrl
@ stub CoInternetQueryInfo
@ stub CopyBindInfo
@ stub CopyStgMedium
@ stub CreateAsyncBindCtx
@ stub CreateAsyncBindCtxEx
@ stub CreateFormatEnumerator
@ stdcall CreateURLMoniker(ptr str ptr) CreateURLMoniker
@ stdcall DllCanUnloadNow() URLMON_DllCanUnloadNow
@ stdcall DllGetClassObject(ptr ptr ptr) URLMON_DllGetClassObject
@ stdcall DllInstall(long ptr) URLMON_DllInstall
@ stdcall DllRegisterServer() URLMON_DllRegisterServer
@ stub DllRegisterServerEx
@ stdcall DllUnregisterServer() URLMON_DllUnregisterServer
@ stub Extract
@ stub FaultInIEFeature
@ stub FindMediaType
@ stub FindMediaTypeClass
@ stub FindMimeFromData
@ stub GetClassFileOrMime
@ stub GetClassURL
@ stub GetComponentIDFromCLSSPEC
@ stub GetMarkOfTheWeb
@ stub GetSoftwareUpdateInfo
@ stub HlinkGoBack
@ stub HlinkGoForward
@ stub HlinkNavigateMoniker
@ stub HlinkNavigateString
@ stub HlinkSimpleNavigateToMoniker
@ stub HlinkSimpleNavigateToString
@ stub IsAsyncMoniker
@ stub IsLoggingEnabledA
@ stub IsLoggingEnabledW
@ stub IsValidURL
@ stub MkParseDisplayNameEx
@ stub ObtainUserAgentString
@ stub PrivateCoInstall
@ stub RegisterBindStatusCallback
@ stub RegisterFormatEnumerator
@ stub RegisterMediaTypeClass
@ stub RegisterMediaTypes
@ stub ReleaseBindInfo
@ stub RevokeBindStatusCallback
@ stub RevokeFormatEnumerator
@ stub SetSoftwareUpdateAdvertisementState
@ stub URLDownloadA
@ stub URLDownloadToCacheFileA
@ stub URLDownloadToCacheFileW
@ stub URLDownloadToFileA
@ stub URLDownloadToFileW
@ stub URLDownloadW
@ stub URLOpenBlockingStreamA
@ stub URLOpenBlockingStreamW
@ stub URLOpenPullStreamA
@ stub URLOpenPullStreamW
@ stub URLOpenStreamA
@ stub URLOpenStreamW
@ stub UrlMkBuildVersion
@ stub UrlMkGetSessionOption
@ stub UrlMkSetSessionOption
@ stub WriteHitLogging
@ stub ZonesReInit

