#pragma once

#include <string>

#include "document_node.h"
#include "office_parser.h"
#include "xlsx_package.h"

namespace office {

struct ResolveResult {
    bool ok = false;
    DocumentNode node;
    std::string error;
};

ResolveResult resolveXlsxGet(const XlsxPackage &package, const parser::PathAst &path_ast, int depth = 1);
ResolveResult resolveXlsxQuery(const XlsxPackage &package, const parser::Selector &selector);

} // namespace office
