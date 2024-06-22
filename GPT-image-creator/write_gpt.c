#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
// =======================================
// Global Typedefs
// =======================================


// MBR Partition
typedef struct {
  uint8_t boot_indicator;
  uint8_t starting_chs[3];
  uint8_t os_type;
  uint8_t ending_chs[3];
  uint32_t starting_lba;
  uint32_t size_lba;
} __attribute__ ((packed)) Mbr_Partition;

// Master Boot Record
typedef struct {
  uint8_t boot_code[440];
  uint32_t mbr_signature;
  uint16_t unknown;
  Mbr_Partition partition[4];
  uint16_t boot_signature;
} __attribute__ ((packed)) Mbr;

// GPT Header
typedef struct {
  uint64_t signature; 
  uint32_t revision;
  uint32_t header_Size;
  uint32_t header_crc32;
  uint32_t reserved_zero;
  uint64_t my_lba;
  uint64_t alternate_lba;
  uint64_t first_usable_lba;
  uint64_t last_usable_lba;
  uint16_t disk_guid[8];
  uint64_t partition_entry_lba;
  uint32_t number_of_partition_entries;
  uint16_t size_of_partition_entry;
  uint16_t partition_entry_array_crc32;
  uint8_t reserved[420]; // BlockSize-92 as in <5.3.2. GPT Header> in spec 
} __attribute__ ((packed)) Gpt;

// GPT Partition
typedef struct {
  unsigned char PartitionTypeGUID;
  unsigned char UniuePartitionGUID;
  uint64_t StartingLBA;
  uint64_t EndingLBA;
  uint64_t Attributes;
  uint16_t PartitionName[36];
  uint64_t Reserved;
} __attribute__ ((packed)) Gpt_Partition;
  


// =======================================
// Global Variables
// =======================================

char *image_name = "test.img";
uint64_t lba_size = 512;
uint64_t esp_size = 1024*1024*33; //33Mb Efi System Partition
uint64_t data_size = 1024*1024*1; //1Mb Data Partition
uint64_t image_size = 0;
uint64_t esp_lbas, data_lbas, image_size_lbas;


//========================
//Pad the 0s to fill the lba to size
//========================
void write_full_lba_size(FILE *image) {
    uint8_t zero_sector[512];
    for (uint8_t i=0; i < (lba_size - sizeof zero_sector) / sizeof zero_sector; i++)
      fwrite(zero_sector, sizeof zero_sector, 1, image);
}


//========================
//Convert bytes to LBAs
//========================

uint64_t bytes_to_lbas(const uint64_t bytes) {
  return (bytes/lba_size) + (bytes % lba_size > 0 ? 1 : 0); // if number of bytes is not divisible by lba_size add 1 for padding.
}



// =======================================
// Write Protective MBR
// =======================================
bool write_mbr(FILE *image) {
  uint64_t mbr_image_lbas = image_size_lbas;
  if (mbr_image_lbas > 0xFFFFFFFF) image_size_lbas = 0x100000000; //used in size_lba to go back to 0xFFFFFFF value as a result (defined by spec).
  Mbr mbr = {
    .boot_code = { 0 },
    .mbr_signature = 0,
    .unknown = 0,
    .partition[0] = {
      .boot_indicator = 0,
      .starting_chs =  {0x00, 0x02, 0x00 },
      .os_type = 0xEE,    // Protective GPT
      .ending_chs = { 0xFF, 0xFF, 0xFF },
      .starting_lba = 0x00000001,
      .size_lba = mbr_image_lbas - 1,
    },
    .boot_signature = 0xAA55,
  };
  // write to file
  if (fwrite(&mbr, 1, sizeof mbr, image) != sizeof mbr)
    return false;

  write_full_lba_size(image);  

  return true;
}

// =======================================
// Write GPT headers and tables
// =======================================
bool write_gpts(FILE *image) {

  Gpt gpt = {
  .signature = 0x5452415020494645,  //EFI PART (ASCII little endian) as of 5.3.2. in UEFI spec.
  .revision = 0x00010000, //1.0 in IEE-754 single precision format
  .header_Size = 92,
  .header_crc32 = 0, //TODO: Compute the 32-bit CRC for HeaderSize bytes
  .reserved_zero = 0,
  .my_lba = 1, //as of 5.3.1. The primary GPT Header must be located in LBA 1 (i.e., the second logical block)
  .alternate_lba= image_size_lbas - 1,
  .first_usable_lba= (16385 / lba_size) + 2, //34 for 512 LBA = A minimum of 16,384 bytes of space must be reserved for the GPT Partition Entry Array. + 1 block for Protective MBR + 1 block for GPT Header
  .last_usable_lba= image_size_lbas - 2, //Speculation based on pure logic ( we need 1 spare LBA for backup partition table and 2nd from the end is a last usable LBA)
  .disk_guid[0] = 0, // TODO: have to get it somewhere.
  .partition_entry_lba=2, // for primary table 
  .number_of_partition_entries=128, //minimum number of tables.
  .size_of_partition_entry=128, //minimum size
  .partition_entry_array_crc32=0, //TODO: Compute the 32-bit CRC for Partition Entry Array
  .reserved[0]= (lba_size-92)*8, 
  };

  return true;
}



//============================================================
//       MAIN
//============================================================

int main(void) {
  FILE *image = fopen(image_name, "wb+");
  if (!image) {
    fprintf(stderr, "Error opening the file: %s\n", image_name);
    return EXIT_FAILURE;
  }

  // Set sizes
  image_size = esp_size + data_size + (1024*1024)/* 1Mb extra padding for GPT/MBR */;
  image_size_lbas = bytes_to_lbas(image_size);

  // Wirte protective MBR 
  if (!write_mbr(image)) {
    fprintf(stderr, "Error writing protective MBR for file: %s\n", image_name);
    return EXIT_FAILURE;
  }
  //Write GPT headers and tables
  if (!write_gpts(image)) {
    fprintf(stderr, "Error writing GPT headers and tables for file: %s\n", image_name);
    return EXIT_FAILURE;
  } 
  return EXIT_SUCCESS;
}
