#include "simpilot/app_module.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace simpilot {

ModuleRegistry::~ModuleRegistry() {
    clear();
}

void ModuleRegistry::clear() noexcept {
    stop();
    // Destruction order matters even for modules that have not been started.
    while (!modules_.empty()) modules_.pop_back();
}

void ModuleRegistry::add(std::string id, std::unique_ptr<IAppModule> module) {
    if (running_ || starting_ || started_ != 0) throw std::logic_error("Modules are already started");
    if (id.empty() || !module) throw std::invalid_argument("Invalid module");
    if (std::ranges::any_of(modules_, [&id](const Entry& entry) {
            return entry.id == id;
        })) {
        throw std::invalid_argument("Duplicate module identifier: " + id);
    }
    modules_.push_back({std::move(id), std::move(module)});
}

void ModuleRegistry::start() {
    if (running_) return;
    if (starting_) throw std::logic_error("Recursive module startup");
    starting_ = true;
    try {
        for (; started_ < modules_.size(); ++started_) {
            try {
                modules_[started_].module->start();
            } catch (...) {
                modules_[started_].module->stop();
                throw;
            }
        }
        running_ = true;
        starting_ = false;
    } catch (...) {
        starting_ = false;
        stop();
        throw;
    }
}

void ModuleRegistry::stop() noexcept {
    while (started_ != 0) modules_[--started_].module->stop();
    running_ = false;
}

} // namespace simpilot
