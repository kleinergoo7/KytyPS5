#include "common/amprPack.h"
#include "common/common.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

void Check(bool value, const char* text) {
  if (!value) {
    std::fprintf(stderr, "AmprPackTests: failed: %s\n", text);
    std::abort();
  }
}

// CRC-32/IEEE, matching the oracle produced by zlib.crc32.
uint32_t Crc32(const uint8_t* data, size_t size) {
  uint32_t crc = 0xffffffffu;
  for (size_t i = 0; i < size; ++i) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
  }
  return ~crc;
}

uint32_t RotateRight(uint32_t value, int bits) {
  return (value >> bits) | (value << (32 - bits));
}

std::string Sha256Hex(const uint8_t* data, size_t size) {
  static constexpr uint32_t kConstant[64] = {
      0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
      0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
      0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
      0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
      0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
      0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
      0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
      0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
  uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                   0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  uint8_t block[64] = {};
  size_t used = 0;
  const auto compress = [&](const uint8_t* p) {
    uint32_t w[64] = {};
    for (int i = 0; i < 16; ++i) {
      w[i] = (uint32_t(p[4 * i]) << 24) | (uint32_t(p[4 * i + 1]) << 16) |
             (uint32_t(p[4 * i + 2]) << 8) | uint32_t(p[4 * i + 3]);
    }
    for (int i = 16; i < 64; ++i) {
      const uint32_t s0 = RotateRight(w[i - 15], 7) ^ RotateRight(w[i - 15], 18) ^ (w[i - 15] >> 3);
      const uint32_t s1 = RotateRight(w[i - 2], 17) ^ RotateRight(w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
    uint32_t e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int i = 0; i < 64; ++i) {
      const uint32_t s1 = RotateRight(e, 6) ^ RotateRight(e, 11) ^ RotateRight(e, 25);
      const uint32_t ch = (e & f) ^ (~e & g);
      const uint32_t t1 = hh + s1 + ch + kConstant[i] + w[i];
      const uint32_t s0 = RotateRight(a, 2) ^ RotateRight(a, 13) ^ RotateRight(a, 22);
      const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
      const uint32_t t2 = s0 + maj;
      hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d;
    h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
  };
  uint64_t bits = 0;
  for (size_t i = 0; i < size; ++i) {
    block[used++] = data[i];
    bits += 8;
    if (used == sizeof(block)) {
      compress(block);
      used = 0;
    }
  }
  block[used++] = 0x80;
  if (used > 56) {
    while (used < sizeof(block)) block[used++] = 0;
    compress(block);
    used = 0;
  }
  while (used < 56) block[used++] = 0;
  for (int i = 7; i >= 0; --i) block[used++] = uint8_t(bits >> (8 * i));
  compress(block);
  char hex[65] = {};
  for (int i = 0; i < 8; ++i) {
    std::snprintf(hex + 8 * i, 9, "%08x", h[i]);
  }
  return std::string(hex, 64);
}

class TempDirectory {
public:
  TempDirectory() {
    m_path = std::filesystem::temp_directory_path() /
             ("kyty_ampr_test_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Check(std::filesystem::create_directories(m_path), "create temporary directory");
  }
  ~TempDirectory() {
    std::error_code error;
    std::filesystem::remove_all(m_path, error);
  }
  [[nodiscard]] const std::filesystem::path& Path() const { return m_path; }
  KYTY_CLASS_NO_COPY(TempDirectory);
private:
  std::filesystem::path m_path;
};

}  // namespace

int main() {
  // Golden oracle: the same font read out of /mnt/hdd/roms/ps5/ASTRO_BOT.zar via gamefs.Zar.
  constexpr uint32_t kFontCrc = 0xfcd88cddu;
  constexpr const char* kFontSha256 = "215c175ce595d0f42bc1f703ef94ecbf6f45dc875cdbfd7ea59d9d8b28467cf9";
  constexpr uint64_t kChunkSize = 262144;
  constexpr uint64_t kChunksOff = 5335424;

  const std::filesystem::path root = "/home/reng/disk/Downloads/[ASTRO BOT]-[PPSA21567]";
  const auto indexPath = root / "ampr_zstd.index";
  Check(std::filesystem::is_regular_file(indexPath), "dump is present");

  Common::AmprPack pack;
  Check(pack.Init(root), "Init on real dump");
  Common::AmprPack::FileInfo font{};
  Check(pack.Lookup("/app0/data/common/font/FuturaStd-Bold.otf", font), "exact-case lookup");
  Check(font.size == 28280 && font.firstChunk == 587370, "font metadata");
  Check(pack.Exists("/app0/data/common/font/FuturaStd-Bold.otf"), "Exists wrapper");

  Common::AmprPack::FileInfo folded{};
  Check(pack.Lookup("/app0/DATA/COMMON/FONT/FUTURASTD-BOLD.OTF", folded), "mixed-case lookup");
  Check(folded.size == font.size && folded.firstChunk == font.firstChunk, "mixed-case resolves to same file");

  std::vector<uint8_t> bytes(font.size);
  Check(pack.Read(font, 0, bytes.size(), bytes.data()), "read whole font");
  Check(Crc32(bytes.data(), bytes.size()) == kFontCrc, "font CRC-32 matches oracle");
  Check(Sha256Hex(bytes.data(), bytes.size()) == kFontSha256, "font SHA-256 matches oracle");
  uint8_t last = 0;
  Check(pack.Read(font, font.size - 1, 1, &last) && last == bytes.back(), "read last byte");

  Common::AmprPack::FileInfo big{};
  Check(pack.Lookup("/app0/data/prein/bg/gfx/big_tornado_scifi_parts_a_05_1v.jxm", big), "large file lookup");
  Check(big.size == 899952 && big.firstChunk == 478528, "large file metadata");
  Check(big.chunkCount == (big.size + kChunkSize - 1) / kChunkSize, "large file chunk count");
  std::vector<uint8_t> whole(big.size);
  std::vector<uint8_t> chunk(kChunkSize);
  for (uint32_t k = 0; k < big.chunkCount; ++k) {
    const uint64_t base = uint64_t(k) * kChunkSize;
    const size_t decoded = size_t(std::min<uint64_t>(kChunkSize, big.size - base));
    Check(pack.Read(big, base, decoded, chunk.data()), "read one full decoded chunk");
    Check(pack.Read(big, base, decoded, whole.data() + base), "read chunk into range buffer");
  }
  constexpr uint64_t crossOffset = kChunkSize - 32;
  constexpr size_t crossLength = 64;
  std::vector<uint8_t> cross(crossLength);
  Check(pack.Read(big, crossOffset, crossLength, cross.data()), "read across chunk boundary");
  Check(std::memcmp(cross.data(), whole.data() + crossOffset, crossLength) == 0,
        "cross-boundary range matches per-chunk reads");

  TempDirectory temporary;
  const auto truncatedRoot = temporary.Path() / "truncated";
  Check(std::filesystem::create_directories(truncatedRoot), "create truncated fixture root");
  {
    std::ifstream source(indexPath, std::ios::binary);
    std::ofstream dest(truncatedRoot / "ampr_zstd.index", std::ios::binary);
    char head[4096] = {};
    source.read(head, sizeof(head));
    dest.write(head, source.gcount());
  }
  Common::AmprPack truncated;
  Check(!truncated.Init(truncatedRoot), "Init rejects truncated index");

  const auto badOffsetRoot = temporary.Path() / "bad-offset";
  Check(std::filesystem::create_directories(badOffsetRoot), "create bad-offset fixture root");
  {
    std::ifstream source(indexPath, std::ios::binary);
    std::ofstream dest(badOffsetRoot / "ampr_zstd.index", std::ios::binary);
    char header[128] = {};
    source.read(header, sizeof(header));
    const uint64_t badOffset = UINT64_MAX;
    std::memcpy(header + 64, &badOffset, sizeof(badOffset));  // chunks_off
    dest.write(header, sizeof(header));
  }
  Common::AmprPack badOffset;
  Check(!badOffset.Init(badOffsetRoot), "Init rejects out-of-range table offset");

  const auto corruptRoot = temporary.Path() / "corrupt";
  Check(std::filesystem::create_directories(corruptRoot), "create corrupt fixture root");
  const auto corruptIndex = corruptRoot / "ampr_zstd.index";
  Check(std::filesystem::copy_file(indexPath, corruptIndex), "copy index for corruption");
  {
    std::fstream file(corruptIndex, std::ios::in | std::ios::out | std::ios::binary);
    file.seekg(static_cast<std::streamoff>(kChunksOff + uint64_t(font.firstChunk) * 8));
    uint64_t word = 0;
    file.read(reinterpret_cast<char*>(&word), sizeof(word));
    Check(file.good(), "read chunk word");
    word = (word & ~(0xfffuLL << 32)) | (0xfffuLL << 32);  // volume id past the volume table
    file.seekp(static_cast<std::streamoff>(kChunksOff + uint64_t(font.firstChunk) * 8));
    file.write(reinterpret_cast<const char*>(&word), sizeof(word));
    Check(file.good(), "write corrupt chunk word");
  }
  Common::AmprPack corrupt;
  Check(corrupt.Init(corruptRoot), "corrupt index still parses");
  Common::AmprPack::FileInfo corruptFont{};
  Check(corrupt.Lookup("/app0/data/common/font/FuturaStd-Bold.otf", corruptFont), "corrupt index lookup");
  std::vector<uint8_t> ignored(font.size);
  Check(!corrupt.Read(corruptFont, 0, ignored.size(), ignored.data()), "corrupt chunk word fails Read");

  std::printf("AmprPackTests: all cases passed\n");
  return 0;
}
