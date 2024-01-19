/**
 * Copyright CERN; j.lopez@cern.ch, florine.de.geus@cern.ch
 */

#include "rntuple_dumper.hxx"

#include <filesystem>
#include <unistd.h>

using RNTupleReadOptions = ROOT::Experimental::RNTupleReadOptions;

enum EDumpFlags {
  kDumpNone = 0,
  kDumpPages = 0x01,
  kDumpMetadata = 0x02,
};

[[noreturn]] static void Usage(char *argv0) {
  printf("Usage: %s [-h] [-m|-a|-f field-name] [-o output-path] file-name "
         "ntuple-name\n\n",
         argv0);
  printf("Options:\n");
  printf("  -h\t\t\tPrint this text\n");
  printf("  -m\t\t\tDump ntuple metadata\n");
  printf("  -a\t\t\tDump pages for all the columns\n");
  printf("  -f field-name\t\tDump pages for all the columns part of the "
         "provided field\n");
  printf("  -o output-path\tGenerated files will be written to output-path "
         "(defaults to `./`)\n");
  printf("\nAt least one of `-m`, `-a` or `-f` is required.\n");
  exit(0);
}

int main(int argc, char *argv[]) {
  std::string inputFile;
  std::string ntupleName;
  std::string outputPath{"./"};
  std::string filenameTmpl{"cluster%d_%s_pg%d.page"};
  std::string fieldName{""};
  unsigned dumpFlags = kDumpNone;

  int c;
  while ((c = getopt(argc, argv, "hmaf:o:")) != -1) {
    switch (c) {
    case 'm':
      dumpFlags |= kDumpMetadata;
      break;
    case 'a':
      dumpFlags |= kDumpPages;
      break;
    case 'f':
      dumpFlags |= kDumpPages;
      fieldName = optarg;
      break;
    case 'o':
      outputPath = optarg;
      break;
    case 'h':
    default:
      Usage(argv[0]);
    }
  }
  if ((argc - optind) != 2 || dumpFlags == kDumpNone)
    Usage(argv[0]);

  if (!std::filesystem::is_directory(outputPath)) {
    fprintf(stderr, "'%s' is not a directory\n", outputPath.c_str());
    exit(1);
  }

  inputFile = argv[optind];
  ntupleName = argv[optind + 1];

  auto source =
      RPageSource::Create(ntupleName, inputFile, RNTupleReadOptions());
  source->Attach();

  RNTupleDumper dumper(std::move(source));
  if (dumpFlags & kDumpMetadata) {
    dumper.DumpMetadata(outputPath);
  }
  if (dumpFlags & kDumpPages) {
    auto columns = dumper.CollectColumns(fieldName);
    for (const auto &column : columns) {
      printf("Column %lu: %s[%lu]\n",
             (unsigned long)column.fColumnDesc.GetPhysicalId(),
             column.fFieldDesc.GetFieldName().c_str(),
             (unsigned long)column.fColumnDesc.GetIndex());
    }
    dumper.DumpPages(columns, outputPath, filenameTmpl);
  }

  return 0;
}
