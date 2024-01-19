/**
 * Copyright CERN; j.lopez@cern.ch, florine.de.geus@cern.ch
 */

#include <ROOT/RColumnElement.hxx>
#include <ROOT/RNTupleDescriptor.hxx>
#include <ROOT/RNTupleSerialize.hxx>
#include <ROOT/RNTupleZip.hxx>
#include <ROOT/RPageStorage.hxx>

#include <bitset>
#include <cstddef>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>

using ClusterSize_t = ROOT::Experimental::ClusterSize_t;
using DescriptorId_t = ROOT::Experimental::DescriptorId_t;
using EColumnType = ROOT::Experimental::EColumnType;
using RClusterIndex = ROOT::Experimental::RClusterIndex;
using RColumnDescriptor = ROOT::Experimental::RColumnDescriptor;
using RColumnElementBase = ROOT::Experimental::Detail::RColumnElementBase;
using RFieldDescriptor = ROOT::Experimental::RFieldDescriptor;
using RNTupleDecompressor = ROOT::Experimental::Internal::RNTupleDecompressor;
using RNTupleDescriptor = ROOT::Experimental::RNTupleDescriptor;
using RNTupleSerializer = ROOT::Experimental::Internal::RNTupleSerializer;
using RPageSource = ROOT::Experimental::Detail::RPageSource;
using RPageStorage = ROOT::Experimental::Detail::RPageStorage;

class RNTupleDumper {
  std::unique_ptr<RPageSource> fSource;

  struct RColumnInfo {
    const RColumnDescriptor &fColumnDesc;
    const RFieldDescriptor &fFieldDesc;
    const std::string fQualName;

    RColumnInfo(const RColumnDescriptor &columnDesc,
                const RFieldDescriptor &fieldDesc)
        : fColumnDesc(columnDesc), fFieldDesc(fieldDesc),
          fQualName(fieldDesc.GetFieldName() + "-" +
                    std::to_string(columnDesc.GetIndex())) {}
  };

  void AddColumnsFromField(std::vector<RColumnInfo> &vec,
                           const RNTupleDescriptor &desc,
                           const RFieldDescriptor &fieldDesc) {
    for (const auto &column : desc.GetColumnIterable(fieldDesc)) {
      vec.emplace_back(column, fieldDesc);
    }

    for (const auto &field : desc.GetFieldIterable(fieldDesc)) {
      AddColumnsFromField(vec, desc, field);
    }
  }

public:
  RNTupleDumper(std::unique_ptr<RPageSource> source)
      : fSource(std::move(source)) {}

  /// Recursively collect all the columns for all the fields rooted at field
  /// zero.
  std::vector<RColumnInfo> CollectColumns(std::string_view fieldName) {
    auto desc = fSource->GetSharedDescriptorGuard();
    std::vector<RColumnInfo> columns;
    DescriptorId_t fieldId = desc->GetFieldZeroId();

    if (!fieldName.empty()) {
      fieldId = desc->FindFieldId(fieldName);
      if (fieldId == ROOT::Experimental::kInvalidDescriptorId) {
        fprintf(stderr, "Field with name '%s' does not exist\n",
                fieldName.data());
        exit(1);
      }
    }

    AddColumnsFromField(columns, desc.GetRef(),
                        desc->GetFieldDescriptor(fieldId));

    return columns;
  }

  /// Iterate over all the clusters and dump the contents of each page for each
  /// column. Generated file names follow the template `filenameTmpl` and are
  /// placed in directory `outputPath`.
  /// TODO(jalopezg): format filenames according to the provided template
  void DumpPages(const std::vector<RColumnInfo> &columns,
                 const std::string &outputPath,
                 const std::string & /*filenameTmpl*/) {
    auto desc = fSource->GetSharedDescriptorGuard();
    std::uint64_t count = 0;
    for (const auto &cluster : desc->GetClusterIterable()) {
      printf("\rDumping pages... [%lu / %lu clusters processed]", ++count,
             desc->GetNClusters());
      for (const auto &column : columns) {
        auto columnId = column.fColumnDesc.GetPhysicalId();
        if (!cluster.ContainsColumn(columnId))
          continue;

        const auto &pages = cluster.GetPageRange(columnId);
        size_t clusterIdx = 0, pageNum = 0;
        for (auto &pageInfo : pages.fPageInfos) {
          RClusterIndex index(cluster.GetId(), clusterIdx);
          RPageStorage::RSealedPage sealedPage;
          fSource->LoadSealedPage(columnId, index, sealedPage);
          auto buffer = std::make_unique<unsigned char[]>(sealedPage.fSize);
          sealedPage.fBuffer = buffer.get();
          fSource->LoadSealedPage(columnId, index, sealedPage);
          {
            std::ostringstream oss(outputPath, std::ios_base::ate);
            oss << "/cluster" << cluster.GetId() << "_" << column.fQualName
                << "_pg" << pageNum++ << ".page";
            std::ofstream of(oss.str(), std::ios_base::binary);
            of.write(reinterpret_cast<const char *>(sealedPage.fBuffer),
                     sealedPage.fSize);
          }
          clusterIdx += pageInfo.fNElements;
        }
      }
    }
    printf("\nDumped data in %lu clusters!\n", count);
  }

  /// Dump ntuple header and footer to separate files.
  void DumpMetadata(const std::string &outputPath) {
    printf("Dumping ntuple metadata...\n");

    auto desc = fSource->GetSharedDescriptorGuard();
    auto context = RNTupleSerializer::SerializeHeader(nullptr, desc.GetRef());
    auto szHeader = context.GetHeaderSize();
    auto headerBuffer = std::make_unique<unsigned char[]>(szHeader);
    context =
        RNTupleSerializer::SerializeHeader(headerBuffer.get(), desc.GetRef());
    {
      std::ofstream of(outputPath + "/header", std::ios_base::binary);
      of.write(reinterpret_cast<char *>(headerBuffer.get()), szHeader);
    }

    for (const auto &clusterGroup : desc->GetClusterGroupIterable()) {
      std::vector<DescriptorId_t> physClusterIds;
      for (const auto &C : clusterGroup.GetClusterIds())
        physClusterIds.emplace_back(context.MapClusterId(C));
      context.MapClusterGroupId(clusterGroup.GetId());

      {
        auto szPageList = RNTupleSerializer::SerializePageList(
            nullptr, desc.GetRef(), physClusterIds, context);
        auto pageListBuffer = std::make_unique<unsigned char[]>(szPageList);
        RNTupleSerializer::SerializePageList(
            pageListBuffer.get(), desc.GetRef(), physClusterIds, context);
        std::ofstream of(outputPath + "/cg" +
                             std::to_string(clusterGroup.GetId()) + ".pagelist",
                         std::ios_base::binary);
        of.write(reinterpret_cast<char *>(pageListBuffer.get()), szPageList);
      }
    }

    auto szFooter =
        RNTupleSerializer::SerializeFooter(nullptr, desc.GetRef(), context);
    auto footerBuffer = std::make_unique<unsigned char[]>(szFooter);
    RNTupleSerializer::SerializeFooter(footerBuffer.get(), desc.GetRef(),
                                       context);
    {
      std::ofstream of(outputPath + "/footer", std::ios_base::binary);
      of.write(reinterpret_cast<char *>(footerBuffer.get()), szFooter);
    }
  }

  /// Iterate over all the clusters and dump the uncompressed contents of each
  /// page for a given column.
  void DumpColumnData(DescriptorId_t columnId, std::uint64_t nElements,
                      std::ostream &output) {
    auto desc = fSource->GetSharedDescriptorGuard();
    const auto &column = desc->GetColumnDescriptor(columnId);
    auto dataBuffer = std::make_unique<unsigned char[]>(0xffffff);

    std::uint64_t elementsProcessed = 0;
    for (const auto &cluster : desc->GetClusterIterable()) {
      if (!cluster.ContainsColumn(columnId))
        continue;

      const auto &pages = cluster.GetPageRange(columnId);
      size_t clusterIdx = 0;
      for (auto &pageInfo : pages.fPageInfos) {
        RClusterIndex index(cluster.GetId(), clusterIdx);
        RPageStorage::RSealedPage sealedPage;
        fSource->LoadSealedPage(columnId, index, sealedPage);
        auto buffer = std::make_unique<unsigned char[]>(sealedPage.fSize);
        sealedPage.fBuffer = buffer.get();
        fSource->LoadSealedPage(columnId, index, sealedPage);
        auto columnElement =
            RColumnElementBase::Generate<void>(column.GetModel().GetType());
        auto page = fSource->UnsealPage(sealedPage, *columnElement, columnId);

        auto buff = static_cast<char *>(page.GetBuffer());
        unsigned int buffIdx = 0;

        while (buffIdx < page.GetNBytes()) {
          if (++elementsProcessed > nElements)
            return;

          switch (column.GetModel().GetType()) {
          case EColumnType::kIndex64:
          case EColumnType::kIndex32:
          case EColumnType::kSplitIndex64:
          case EColumnType::kSplitIndex32:
            output << static_cast<ClusterSize_t>(buff[buffIdx]) << std::endl;
            break;
          case EColumnType::kUInt64:
          case EColumnType::kSplitUInt64:
            output << static_cast<std::uint64_t>(buff[buffIdx]) << std::endl;
            break;
          case EColumnType::kUInt32:
          case EColumnType::kSplitUInt32:
            output << static_cast<std::uint32_t>(buff[buffIdx]) << std::endl;
            break;
          case EColumnType::kUInt16:
          case EColumnType::kSplitUInt16:
            output << static_cast<std::uint16_t>(buff[buffIdx]) << std::endl;
            break;
          case EColumnType::kUInt8:
            output << static_cast<std::uint8_t>(buff[buffIdx]) << std::endl;
            break;
          case EColumnType::kInt64:
          case EColumnType::kSplitInt64:
            output << static_cast<std::int64_t>(buff[buffIdx]) << std::endl;
            break;
          case EColumnType::kSplitInt32:
          case EColumnType::kInt32:
            output << static_cast<std::int32_t>(buff[buffIdx]) << std::endl;
            break;
          case EColumnType::kInt16:
          case EColumnType::kSplitInt16:
            output << static_cast<std::int16_t>(buff[buffIdx]) << std::endl;
            break;
          case EColumnType::kInt8:
            output << static_cast<std::int8_t>(buff[buffIdx]) << std::endl;
            break;
          case EColumnType::kReal64:
          case EColumnType::kSplitReal64:
            output << static_cast<double>(buff[buffIdx]) << std::endl;
            break;
          case EColumnType::kReal32:
          case EColumnType::kSplitReal32:
          case EColumnType::kReal16:
            output << *reinterpret_cast<float *>(&(buff[buffIdx])) << std::endl;
            break;
          case EColumnType::kByte:
            output << static_cast<std::bitset<8>>(buff[buffIdx]) << std::endl;
            break;
          case EColumnType::kChar:
            output << static_cast<char>(buff[buffIdx]) << std::endl;
            break;
          case EColumnType::kBit:
            output << static_cast<bool>(buff[buffIdx]) << std::endl;
            break;
          case EColumnType::kSwitch:
          default:
            R__ASSERT(false);
          }

          buffIdx += page.GetElementSize();
        }

        clusterIdx += pageInfo.fNElements;
      }
    }
  }
};
