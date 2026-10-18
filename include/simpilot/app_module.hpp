#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace simpilot {

class IAppModule {
public:
    virtual ~IAppModule() = default;
    virtual void start() = 0;
    virtual void stop() noexcept = 0;
};

class ModuleRegistry final {
public:
    ~ModuleRegistry();
    ModuleRegistry() = default;
    ModuleRegistry(const ModuleRegistry&) = delete;
    ModuleRegistry& operator=(const ModuleRegistry&) = delete;

    void add(std::string id, std::unique_ptr<IAppModule> module);
    void start();
    void stop() noexcept;
    void clear() noexcept;
    [[nodiscard]] std::size_t size() const noexcept { return modules_.size(); }
    [[nodiscard]] bool running() const noexcept { return running_; }

private:
    struct Entry {
        std::string id;
        std::unique_ptr<IAppModule> module;
    };
    std::vector<Entry> modules_;
    std::size_t started_ = 0;
    bool running_ = false;
    bool starting_ = false;
};

} // namespace simpilot
