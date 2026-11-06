#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "xlsx_package.h"

namespace office {

struct XlsxSheetInfo {
    std::string name;
    int sheet_id = 0;
    std::string relationship_id;
    std::string worksheet_part;
    bool worksheet_part_exists = false;
};

struct XlsxWorkbook {
    std::vector<XlsxSheetInfo> sheets;
    std::vector<std::string> shared_strings;
};

struct XlsxWorkbookResult {
    bool ok = false;
    XlsxWorkbook workbook;
    std::string error;
};

XlsxWorkbookResult loadWorkbook(const XlsxPackage &package);
std::string toJson(const XlsxWorkbook &workbook);

} // namespace office
