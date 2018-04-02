#include <stdio.h>
#include <clang-c/Index.h>

enum CXChildVisitResult visitor (CXCursor cursor, CXCursor parent,
                                 CXClientData client_data) {
  if (!clang_Location_isFromMainFile(clang_getCursorLocation(cursor))) {
    return CXChildVisit_Continue;
  }
  const char* name = clang_getCString(clang_getCursorSpelling(cursor));
  const char* kind = clang_getCString
      (clang_getCursorKindSpelling(clang_getCursorKind(cursor)));
  
  printf("Cursor '%s' of kind '%s'\n", name, kind);
  return CXChildVisit_Recurse;
}

int main(const int argc, const char* argv[]) {
  if (argc < 2) {
    fprintf(stderr, "Please give the path to the input file as argument.\n");
    return -1;
  }
  
  CXIndex index = clang_createIndex(0, 0);
  CXTranslationUnit unit = clang_parseTranslationUnit(index, argv[1], NULL, 0,
                                                      NULL, 0,
                                                      CXTranslationUnit_None);
  if (unit == NULL) {
    fprintf(stderr, "Unable to parse '%s'. Quitting.\n", argv[1]);
    return -1;
  }
  
  CXCursor cursor = clang_getTranslationUnitCursor(unit);
  clang_visitChildren(cursor, &visitor, NULL);
  
  clang_disposeTranslationUnit(unit);
  clang_disposeIndex(index);
}
