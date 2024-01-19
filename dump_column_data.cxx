/**
 * Copyright CERN; florine.de.geus@cern.ch
 */

#include "rntuple_dumper.hxx"

#include <filesystem>
#include <unistd.h>

using RNTupleReadOptions = ROOT::Experimental::RNTupleReadOptions;

[[noreturn]] static void Usage(char *argv0) {
  printf("Usage: %s [-h] [-o output-path] [-n n-entries] column-id file-name "
         "ntuple-name\n\n",
         argv0);
  printf("Options:\n");
  printf("  -h\t\t\tPrint this text\n");
  printf("  -n n-entries\t\tDump up to N entries\n");
  printf("  -o output-path\tData will be written to output-path (default is "
         "stdout)\n");
  exit(0);
}

int main(int argc, char *argv[]) {
  DescriptorId_t columnId;
  std::string inputFile;
  std::string ntupleName;
  std::string outputPath;
  std::uint64_t nMaxElements = 0;

  int c;
  while ((c = getopt(argc, argv, "hn:o:")) != -1) {
    switch (c) {
    case 'n':
      nMaxElements = std::stoul(optarg);
      break;
    case 'o':
      outputPath = optarg;
      break;
    case 'h':
    default:
      Usage(argv[0]);
    }
  }
  if ((argc - optind) != 3)
    Usage(argv[0]);

  if (!outputPath.empty() &&
      !std::filesystem::path(outputPath).has_parent_path()) {
    fprintf(stderr,
            "'%s' is not a valid path (check if all directories exist)\n",
            outputPath.c_str());
    exit(1);
  }

  auto buf = std::cout.rdbuf();
  std::ofstream os;

  if (!outputPath.empty()) {
    os.open(outputPath);
    buf = os.rdbuf();
  }

  std::ostream output(buf);

  columnId = std::stoul(argv[optind]);
  inputFile = argv[optind + 1];
  ntupleName = argv[optind + 2];

  std::cout << "Reading column " << columnId << " from '" << ntupleName
            << "' (in '" << inputFile << "')" << std::endl;

  auto source =
      RPageSource::Create(ntupleName, inputFile, RNTupleReadOptions());
  source->Attach();

  RNTupleDumper dumper(std::move(source));
  dumper.DumpColumnData(columnId, nMaxElements, output);

  return 0;
}
