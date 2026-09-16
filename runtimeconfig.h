#ifndef RUNTIMECONFIG_H
#define RUNTIMECONFIG_H

#include <array>
#include <atomic>

class TimeConfigManager
{
public:
    TimeConfigManager();

    double globalIntervalSeconds() const;
    double releaseIntervalSeconds() const;
    double keyIntervalSeconds(int index) const;

    void setGlobalIntervalSeconds(double value);
    void setReleaseIntervalSeconds(double value);
    void setKeyIntervalSeconds(int index, double value);

private:
    std::atomic<double> globalInterval;
    std::atomic<double> releaseInterval;
    std::array<std::atomic<double>, 10> keyIntervals;
};

class KeyConfigManager
{
public:
    KeyConfigManager();

    bool isEnabled(int index) const;
    void setEnabled(int index, bool enabled);

private:
    std::array<std::atomic<bool>, 10> enabledKeys;
};

#endif // RUNTIMECONFIG_H
