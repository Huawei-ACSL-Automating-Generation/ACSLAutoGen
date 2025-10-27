#ifndef __ACSLG_SRC_CONFIG_H__
#define __ACSLG_SRC_CONFIG_H__

namespace acslg {
    struct GlobalConfig {
        unsigned acslLabelSuffixLength{4};
        // more args...

        static GlobalConfig &instance() {
            static GlobalConfig cfg;
            return cfg;
        }

        GlobalConfig()                                = default;
        GlobalConfig(const GlobalConfig &)            = delete;
        GlobalConfig &operator=(const GlobalConfig &) = delete;
    };
} // namespace acslg

#endif
