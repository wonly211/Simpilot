#include "apps_folder_application_catalog.hpp"

#include <Windows.h>
#include <propkey.h>
#include <propsys.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wrl/client.h>

#include <algorithm>
#include <cwctype>
#include <unordered_set>

namespace simpilot {
namespace {

using Microsoft::WRL::ComPtr;

class Apartment final {
public:
    Apartment() : result_(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)) {}
    ~Apartment() { if (SUCCEEDED(result_)) CoUninitialize(); }
    [[nodiscard]] bool available() const noexcept {
        return SUCCEEDED(result_) || result_ == RPC_E_CHANGED_MODE;
    }

private:
    HRESULT result_;
};

std::wstring lowercase(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), towlower);
    return value;
}

std::wstring take_string(LPWSTR value) {
    if (!value) return {};
    std::wstring result(value);
    CoTaskMemFree(value);
    return result;
}

} // namespace

bool AppsFolderApplicationCatalog::is_launchable(
    const AppsFolderApplication& application) noexcept {
    return !application.display_name.empty()
        && !application.aumid.empty();
}

bool AppsFolderApplicationCatalog::is_packaged(
    const AppsFolderApplication& application) noexcept {
    return is_launchable(application) && !application.package_family_name.empty();
}

std::vector<AppsFolderApplication> AppsFolderApplicationCatalog::normalize(
    std::vector<AppsFolderApplication> applications,
    const AppsFolderApplicationScope scope) {
    std::erase_if(applications, [scope](const auto& application) {
        return !is_launchable(application)
            || (scope == AppsFolderApplicationScope::packaged
                && !is_packaged(application));
    });
    std::sort(applications.begin(), applications.end(), [](const auto& left, const auto& right) {
        return lowercase(left.aumid) < lowercase(right.aumid);
    });
    std::vector<AppsFolderApplication> unique;
    unique.reserve(applications.size());
    std::unordered_set<std::wstring> identities;
    for (auto& application : applications) {
        if (!identities.insert(lowercase(application.aumid)).second) continue;
        unique.push_back(std::move(application));
    }
    std::sort(unique.begin(), unique.end(), [](const auto& left, const auto& right) {
        const auto left_name = lowercase(left.display_name);
        const auto right_name = lowercase(right.display_name);
        return left_name != right_name
            ? left_name < right_name
            : lowercase(left.aumid) < lowercase(right.aumid);
    });
    return unique;
}

std::vector<AppsFolderApplication> AppsFolderApplicationCatalog::enumerate(
    const AppsFolderApplicationScope scope) {
    const Apartment apartment;
    if (!apartment.available()) return {};

    ComPtr<IShellItem> folder;
    if (FAILED(SHGetKnownFolderItem(FOLDERID_AppsFolder, KF_FLAG_DEFAULT, nullptr,
            IID_PPV_ARGS(folder.GetAddressOf())))) {
        return {};
    }
    ComPtr<IEnumShellItems> enumerator;
    if (FAILED(folder->BindToHandler(nullptr, BHID_EnumItems,
            IID_PPV_ARGS(enumerator.GetAddressOf())))) {
        return {};
    }

    std::vector<AppsFolderApplication> applications;
    // PackageFamilyName is exposed by the shell but has no public PKEY constant.
    PROPERTYKEY package_family_key{};
    const auto has_package_family_key = SUCCEEDED(PSGetPropertyKeyFromName(
        L"System.AppUserModel.PackageFamilyName", &package_family_key));
    ComPtr<IShellItem> item;
    while (enumerator->Next(1, item.ReleaseAndGetAddressOf(), nullptr) == S_OK) {
        ComPtr<IShellItem2> item2;
        if (SUCCEEDED(item.As(&item2))) {
            BOOL separator = FALSE;
            if (SUCCEEDED(item2->GetBool(
                    PKEY_AppUserModel_IsDestListSeparator, &separator))
                && separator) {
                item.Reset();
                continue;
            }
            LPWSTR display_name = nullptr;
            LPWSTR package_family_name = nullptr;
            LPWSTR aumid = nullptr;
            AppsFolderApplication application;
            if (SUCCEEDED(item2->GetDisplayName(SIGDN_NORMALDISPLAY, &display_name))) {
                application.display_name = take_string(display_name);
            }
            if (has_package_family_key && SUCCEEDED(item2->GetString(
                    package_family_key, &package_family_name))) {
                application.package_family_name = take_string(package_family_name);
            }
            if (SUCCEEDED(item2->GetString(PKEY_AppUserModel_ID, &aumid))) {
                application.aumid = take_string(aumid);
            }
            if (is_launchable(application)
                && (scope == AppsFolderApplicationScope::all
                    || is_packaged(application))) {
                applications.push_back(std::move(application));
            }
        }
        item.Reset();
    }
    return normalize(std::move(applications), scope);
}

std::wstring AppsFolderApplicationCatalog::shell_target(const std::wstring_view aumid) {
    if (aumid.empty()) return {};
    return std::wstring(L"shell:AppsFolder\\") + std::wstring(aumid);
}

} // namespace simpilot
