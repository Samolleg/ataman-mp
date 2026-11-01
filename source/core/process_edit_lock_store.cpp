#if defined(_WIN32) && (!defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0600)
// Known Folder API is Vista+. Limit the SDK target change to this unit.
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include "process_edit_lock_store.h"
#ifdef Q_OS_WIN
// Instantiate SDK known-folder GUIDs in this translation unit (old MinGW kit).
#include <initguid.h>
#include <knownfolders.h>
#include <shlobj.h>
#include <aclapi.h>
#endif

namespace mapper_edit_locks {
QString directory() {
#ifdef EI_MAPPER_LOCK_STORE_TESTING
    const auto testRoot=qEnvironmentVariable("EI_MAPPER_TEST_LOCK_ROOT");
    // Fail closed rather than accidentally touching real ProgramData in a test.
    return testRoot.isEmpty()?QString():QDir(testRoot).filePath("EI Mapper Ataman/locks");
#elif defined(Q_OS_WIN)
    PWSTR path=nullptr;
    const HRESULT result=SHGetKnownFolderPath(FOLDERID_ProgramData,0,nullptr,&path);
    const auto root=SUCCEEDED(result)&&path?QString::fromWCharArray(path):QString();
    CoTaskMemFree(path);
    return root.isEmpty()?QString():QDir(root).filePath("EI Mapper Ataman/locks");
#else
    return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)).filePath("EI Mapper Ataman/locks");
#endif
}

#ifdef Q_OS_WIN
namespace {
const DWORD modify=FILE_GENERIC_READ|FILE_GENERIC_WRITE|FILE_GENERIC_EXECUTE|DELETE;

bool usersCanModify(const QString& path,PSID users) {
    const auto native=QDir::toNativeSeparators(path);
    PACL acl=nullptr;PSECURITY_DESCRIPTOR descriptor=nullptr;
    if(GetNamedSecurityInfoW(const_cast<LPWSTR>(reinterpret_cast<LPCWSTR>(native.utf16())),
        SE_FILE_OBJECT,DACL_SECURITY_INFORMATION,nullptr,nullptr,&acl,nullptr,&descriptor)!=ERROR_SUCCESS)return false;
    // A null DACL is not a suitable shared service store.
    bool valid=acl!=nullptr;
    TRUSTEE_W trustee={};BuildTrusteeWithSidW(&trustee,users);
    DWORD rights=0;
    valid=valid&&GetEffectiveRightsFromAclW(acl,&trustee,&rights)==ERROR_SUCCESS
        &&(rights&(modify|FILE_DELETE_CHILD))==(modify|FILE_DELETE_CHILD);
    // Check the ACL inherited by a new lock file as well as this directory.
    // Directory-only write permission would leave other users' locks undeletable.
    if(valid) {
        QByteArray bytes(int(acl->AclSize),0);auto child=reinterpret_cast<PACL>(bytes.data());
        valid=InitializeAcl(child,DWORD(bytes.size()),ACL_REVISION);
        for(DWORD i=0;valid&&i<acl->AceCount;++i) {
            void* raw=nullptr;valid=GetAce(acl,i,&raw);
            if(!valid)break;
            const auto header=static_cast<ACE_HEADER*>(raw);
            if(!(header->AceFlags&OBJECT_INHERIT_ACE))continue;
            QByteArray ace(static_cast<const char*>(raw),header->AceSize);
            reinterpret_cast<ACE_HEADER*>(ace.data())->AceFlags=0;
            valid=AddAce(child,ACL_REVISION,MAXDWORD,ace.data(),DWORD(ace.size()));
        }
        rights=0;
        valid=valid&&GetEffectiveRightsFromAclW(child,&trustee,&rights)==ERROR_SUCCESS
            &&(rights&modify)==modify;
    }
    LocalFree(descriptor);return valid;
}

bool createSharedDirectory(const QString& path,PSID users) {
    EXPLICIT_ACCESS_W access={};
    access.grfAccessPermissions=modify|FILE_DELETE_CHILD;
    access.grfAccessMode=GRANT_ACCESS;
    access.grfInheritance=SUB_CONTAINERS_AND_OBJECTS_INHERIT;
    BuildTrusteeWithSidW(&access.Trustee,users);
    PACL acl=nullptr;
    if(SetEntriesInAclW(1,&access,nullptr,&acl)!=ERROR_SUCCESS)return false;
    SECURITY_DESCRIPTOR descriptor;
    const bool ready=InitializeSecurityDescriptor(&descriptor,SECURITY_DESCRIPTOR_REVISION)
        &&SetSecurityDescriptorDacl(&descriptor,TRUE,acl,FALSE);
    SECURITY_ATTRIBUTES attributes={sizeof(SECURITY_ATTRIBUTES),&descriptor,FALSE};
    const auto native=QDir::toNativeSeparators(path);
    const bool made=ready&&CreateDirectoryW(reinterpret_cast<LPCWSTR>(native.utf16()),&attributes);
    const auto code=GetLastError();LocalFree(acl);
    // A concurrent creator is allowed, but its ACL must pass the same validation.
    return made||(code==ERROR_ALREADY_EXISTS&&QFileInfo(path).isDir());
}
}
#endif

bool prepareDirectory(const QString& path,QString* error) {
    const auto fail=[&](const QString& reason){
        if(error)*error=reason+QDir::toNativeSeparators(path.isEmpty()?QStringLiteral("%ProgramData%/EI Mapper Ataman/locks"):path);
        return false;
    };
    if(path.isEmpty())return fail(QString::fromUtf8("Не удалось определить общую папку блокировок: "));
#ifdef Q_OS_WIN
    BYTE sid[SECURITY_MAX_SID_SIZE];DWORD size=sizeof(sid);
    // Builtin Users, S-1-5-32-545; never depend on the localized group name.
    if(!CreateWellKnownSid(WinBuiltinUsersSid,nullptr,sid,&size))
        return fail(QString::fromUtf8("Не удалось определить права Users (S-1-5-32-545) для папки блокировок: "));
    const auto parent=QFileInfo(path).absolutePath();
    for(const auto& folder:{parent,path}) {
        if(!QFileInfo::exists(folder)&&!createSharedDirectory(folder,sid))
            return fail(QString::fromUtf8("Не удалось создать общую папку блокировок: "));
        if(!QFileInfo(folder).isDir())return fail(QString::fromUtf8("Путь общей папки блокировок занят файлом: "));
    }
    // Do not repair an existing folder or rewrite anyone else's ACL.
    if(!usersCanModify(path,sid))return fail(QString::fromUtf8("Нет общих прав Users (S-1-5-32-545) на чтение, создание, изменение и удаление блокировок. Обратитесь к администратору; папка: "));
#else
    if(!QDir().mkpath(path))return fail(QString::fromUtf8("Не удалось создать общую папку блокировок: "));
#endif
    return true;
}

bool existingOwner(const QString& filename) {
    if(!QFileInfo::exists(filename))return false;
    QLockFile old(filename);old.setStaleLockTime(0);
    qint64 pid=0;QString host,application;
    if(!old.getLockInfo(&pid,&host,&application)||pid<=0)return true;
#ifdef Q_OS_WIN
    // A remote owner's PID cannot be checked on this machine (e.g. old locks
    // beside a map on a share). Unknown/truncated PIDs are not proof of death.
    if(host.compare(QSysInfo::machineHostName(),Qt::CaseInsensitive)!=0||quint64(pid)>MAXDWORD)return true;
    // Access denied (for example another Windows user) is not proof of death.
    const HANDLE process=OpenProcess(SYNCHRONIZE,FALSE,DWORD(pid));
    if(!process)return GetLastError()!=ERROR_INVALID_PARAMETER;
    const auto state=WaitForSingleObject(process,0);CloseHandle(process);
    return state!=WAIT_OBJECT_0;
#else
    // Migration of the Windows store is Windows-specific; fail closed elsewhere.
    return true;
#endif
}
}
