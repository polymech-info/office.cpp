#pragma once

#include "document_node.h"
#include "xlsx_package.h"
#include "xlsx_resolver.h"

namespace office {

ResolveResult resolveOoxmlMetadataGet(const XlsxPackage &package, const std::string &target);

} // namespace office
