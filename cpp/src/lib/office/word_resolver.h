#pragma once

#include "document_node.h"
#include "office_parser.h"
#include "xlsx_package.h"
#include "xlsx_resolver.h"

namespace office {

ResolveResult resolveWordGet(const XlsxPackage &package, const parser::PathAst &path_ast, int depth = 1);
ResolveResult resolveWordQuery(const XlsxPackage &package, const parser::Selector &selector);

} // namespace office
