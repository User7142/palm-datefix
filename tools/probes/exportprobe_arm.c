/* reads export table entry: request = module, index (BE), answer = value (BE) */
typedef unsigned long UInt32;
typedef unsigned char UInt8;

UInt32 Read(const void *e, void *user, void *c)
{
  register UInt32 **tables __asm__("r9");
  UInt8  *r = (UInt8 *)user;
  UInt32  m = ((UInt32)r[0] << 24) | ((UInt32)r[1] << 16) | ((UInt32)r[2] << 8) | r[3];
  UInt32  i = ((UInt32)r[4] << 24) | ((UInt32)r[5] << 16) | ((UInt32)r[6] << 8) | r[7];
  UInt32 *table = *(UInt32 **)((UInt8 *)tables - 4 * (m + 1));
  UInt32  v = table[i];
  r[8] = v >> 24; r[9] = v >> 16; r[10] = v >> 8; r[11] = v;
  return 0;
}
