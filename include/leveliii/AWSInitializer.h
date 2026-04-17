#pragma once

#include <aws/core/Aws.h>
#include <memory>

namespace leveliii {

/**
 * @class AWSInitializer
 * @brief RAII wrapper for AWS SDK initialization and shutdown.
 */
class AWSInitializer {
public:
    AWSInitializer();
    ~AWSInitializer();

    // Prevent copying
    AWSInitializer(const AWSInitializer&) = delete;
    AWSInitializer& operator=(const AWSInitializer&) = delete;

private:
    Aws::SDKOptions options_;
};

} // namespace leveliii
