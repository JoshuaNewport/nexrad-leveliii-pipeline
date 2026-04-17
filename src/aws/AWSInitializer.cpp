#include "leveliii/AWSInitializer.h"
#include <iostream>

namespace leveliii {

AWSInitializer::AWSInitializer() {
    options_.loggingOptions.logLevel = Aws::Utils::Logging::LogLevel::Off;
    Aws::InitAPI(options_);
    std::cout << "✅ AWS SDK Initialized." << std::endl;
}

AWSInitializer::~AWSInitializer() {
    Aws::ShutdownAPI(options_);
    std::cout << "✅ AWS SDK Shutdown." << std::endl;
}

} // namespace leveliii
