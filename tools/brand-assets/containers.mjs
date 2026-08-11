export function createIco(entries) {
  const headerSize = 6 + entries.length * 16;
  const header = Buffer.alloc(headerSize);
  header.writeUInt16LE(0, 0);
  header.writeUInt16LE(1, 2);
  header.writeUInt16LE(entries.length, 4);
  let payloadOffset = headerSize;
  entries.forEach(({ size, png }, index) => {
    const offset = 6 + index * 16;
    header[offset] = size === 256 ? 0 : size;
    header[offset + 1] = size === 256 ? 0 : size;
    header[offset + 2] = 0;
    header[offset + 3] = 0;
    header.writeUInt16LE(1, offset + 4);
    header.writeUInt16LE(32, offset + 6);
    header.writeUInt32LE(png.length, offset + 8);
    header.writeUInt32LE(payloadOffset, offset + 12);
    payloadOffset += png.length;
  });
  return Buffer.concat([header, ...entries.map(({ png }) => png)]);
}

const ICNS_TYPES = new Map([
  [16, "icp4"],
  [32, "icp5"],
  [64, "icp6"],
  [128, "ic07"],
  [256, "ic08"],
  [512, "ic09"],
  [1024, "ic10"],
]);

export function createIcns(entries) {
  const unique = new Map();
  for (const entry of entries) unique.set(entry.size, entry.png);
  const chunks = [...unique.entries()]
    .sort(([left], [right]) => left - right)
    .map(([size, png]) => {
      const type = ICNS_TYPES.get(size);
      if (!type) throw new Error(`unsupported ICNS icon size ${size}`);
      const header = Buffer.alloc(8);
      header.write(type, 0, "ascii");
      header.writeUInt32BE(png.length + 8, 4);
      return Buffer.concat([header, png]);
    });
  const header = Buffer.alloc(8);
  header.write("icns", 0, "ascii");
  header.writeUInt32BE(8 + chunks.reduce((sum, item) => sum + item.length, 0), 4);
  return Buffer.concat([header, ...chunks]);
}

export function readIcoDirectory(buffer) {
  if (buffer.readUInt16LE(0) !== 0 || buffer.readUInt16LE(2) !== 1) {
    throw new Error("invalid ICO header");
  }
  const count = buffer.readUInt16LE(4);
  return Array.from({ length: count }, (_, index) => {
    const offset = 6 + index * 16;
    const width = buffer[offset] || 256;
    const height = buffer[offset + 1] || 256;
    const length = buffer.readUInt32LE(offset + 8);
    const payloadOffset = buffer.readUInt32LE(offset + 12);
    if (payloadOffset + length > buffer.length) throw new Error("ICO payload is out of range");
    return { width, height, length, payloadOffset, planes: buffer.readUInt16LE(offset + 4), bitCount: buffer.readUInt16LE(offset + 6) };
  });
}

export function readIcnsDirectory(buffer) {
  if (buffer.toString("ascii", 0, 4) !== "icns" || buffer.readUInt32BE(4) !== buffer.length) {
    throw new Error("invalid ICNS header or length");
  }
  const entries = [];
  let offset = 8;
  while (offset < buffer.length) {
    const type = buffer.toString("ascii", offset, offset + 4);
    const length = buffer.readUInt32BE(offset + 4);
    if (length < 8 || offset + length > buffer.length) throw new Error("invalid ICNS chunk length");
    entries.push({ type, length, payloadOffset: offset + 8 });
    offset += length;
  }
  return entries;
}
