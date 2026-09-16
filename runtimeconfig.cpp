#include "runtimeconfig.h"

TimeConfigManager::TimeConfigManager()
    : globalInterval(0.1), releaseInterval(0.027)
{
    for (auto& interval : keyIntervals)
        interval.store(1.0);
}

double TimeConfigManager::globalIntervalSeconds() const
{
    return globalInterval.load();
}

double TimeConfigManager::releaseIntervalSeconds() const
{
    return releaseInterval.load();
}

double TimeConfigManager::keyIntervalSeconds(int index) const
{
    return index >= 0 && index < 10 ? keyIntervals[index].load() : 1.0;
}

void TimeConfigManager::setGlobalIntervalSeconds(double value)
{
    globalInterval.store(value);
}

void TimeConfigManager::setReleaseIntervalSeconds(double value)
{
    releaseInterval.store(value);
}

void TimeConfigManager::setKeyIntervalSeconds(int index, double value)
{
    if (index >= 0 && index < 10)
        keyIntervals[index].store(value);
}

KeyConfigManager::KeyConfigManager()
{
    for (auto& enabled : enabledKeys)
        enabled.store(false);
}

bool KeyConfigManager::isEnabled(int index) const
{
    return index >= 0 && index < 10 && enabledKeys[index].load();
}

void KeyConfigManager::setEnabled(int index, bool enabled)
{
    if (index >= 0 && index < 10)
        enabledKeys[index].store(enabled);
}
