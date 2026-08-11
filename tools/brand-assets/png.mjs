import { deflateSync, inflateSync } from "node:zlib";

const PNG_SIGNATURE = Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]);

function crc32(buffer) {
  let crc = 0xffffffff;
  for (const byte of buffer) {
    crc ^= byte;
    for (let bit = 0; bit < 8; bit += 1) {
      crc = (crc >>> 1) ^ (0xedb88320 & -(crc & 1));
    }
  }
  return (crc ^ 0xffffffff) >>> 0;
}

function chunk(type, data) {
  const typeBuffer = Buffer.from(type, "ascii");
  const length = Buffer.alloc(4);
  length.writeUInt32BE(data.length);
  const checksum = Buffer.alloc(4);
  checksum.writeUInt32BE(crc32(Buffer.concat([typeBuffer, data])));
  return Buffer.concat([length, typeBuffer, data, checksum]);
}

export function readPngHeader(buffer) {
  if (!buffer.subarray(0, 8).equals(PNG_SIGNATURE)) {
    throw new Error("invalid PNG signature");
  }
  if (buffer.toString("ascii", 12, 16) !== "IHDR") {
    throw new Error("PNG is missing IHDR");
  }
  return {
    width: buffer.readUInt32BE(16),
    height: buffer.readUInt32BE(20),
    bitDepth: buffer[24],
    colorType: buffer[25],
  };
}

export function decodePng(buffer) {
  const header = readPngHeader(buffer);
  if (header.bitDepth !== 8 || ![2, 6].includes(header.colorType)) {
    throw new Error(`expected 8-bit RGB/RGBA PNG, got depth=${header.bitDepth} type=${header.colorType}`);
  }
  const idat = [];
  let offset = 8;
  while (offset < buffer.length) {
    const length = buffer.readUInt32BE(offset);
    const type = buffer.toString("ascii", offset + 4, offset + 8);
    const data = buffer.subarray(offset + 8, offset + 8 + length);
    if (type === "IDAT") idat.push(data);
    offset += length + 12;
    if (type === "IEND") break;
  }
  const raw = inflateSync(Buffer.concat(idat));
  const channels = header.colorType === 6 ? 4 : 3;
  const stride = header.width * channels;
  const decoded = Buffer.alloc(stride * header.height);
  let sourceOffset = 0;
  for (let y = 0; y < header.height; y += 1) {
    const filter = raw[sourceOffset];
    sourceOffset += 1;
    for (let x = 0; x < stride; x += 1) {
      const value = raw[sourceOffset + x];
      const target = y * stride + x;
      const left = x >= channels ? decoded[target - channels] : 0;
      const up = y > 0 ? decoded[target - stride] : 0;
      const upLeft = y > 0 && x >= channels ? decoded[target - stride - channels] : 0;
      if (filter === 0) decoded[target] = value;
      else if (filter === 1) decoded[target] = (value + left) & 0xff;
      else if (filter === 2) decoded[target] = (value + up) & 0xff;
      else if (filter === 3) decoded[target] = (value + Math.floor((left + up) / 2)) & 0xff;
      else if (filter === 4) decoded[target] = (value + paeth(left, up, upLeft)) & 0xff;
      else throw new Error(`unsupported PNG filter ${filter}`);
    }
    sourceOffset += stride;
  }
  if (channels === 4) return { ...header, pixels: decoded };
  const pixels = Buffer.alloc(header.width * header.height * 4);
  for (let source = 0, target = 0; source < decoded.length; source += 3, target += 4) {
    pixels[target] = decoded[source];
    pixels[target + 1] = decoded[source + 1];
    pixels[target + 2] = decoded[source + 2];
    pixels[target + 3] = 255;
  }
  return { ...header, pixels };
}

function paeth(left, up, upLeft) {
  const prediction = left + up - upLeft;
  const leftDistance = Math.abs(prediction - left);
  const upDistance = Math.abs(prediction - up);
  const diagonalDistance = Math.abs(prediction - upLeft);
  if (leftDistance <= upDistance && leftDistance <= diagonalDistance) return left;
  if (upDistance <= diagonalDistance) return up;
  return upLeft;
}

export function encodePng(image) {
  const { width, height, pixels } = image;
  if (pixels.length !== width * height * 4) throw new Error("RGBA buffer size mismatch");
  const raw = Buffer.alloc((width * 4 + 1) * height);
  for (let y = 0; y < height; y += 1) {
    const rowOffset = y * (width * 4 + 1);
    raw[rowOffset] = 0;
    pixels.copy(raw, rowOffset + 1, y * width * 4, (y + 1) * width * 4);
  }
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(width, 0);
  ihdr.writeUInt32BE(height, 4);
  ihdr[8] = 8;
  ihdr[9] = 6;
  return Buffer.concat([
    PNG_SIGNATURE,
    chunk("IHDR", ihdr),
    chunk("IDAT", deflateSync(raw, { level: 9 })),
    chunk("IEND", Buffer.alloc(0)),
  ]);
}

export function resizeArea(source, width, height = width) {
  const output = Buffer.alloc(width * height * 4);
  const scaleX = source.width / width;
  const scaleY = source.height / height;
  for (let y = 0; y < height; y += 1) {
    const top = y * scaleY;
    const bottom = (y + 1) * scaleY;
    for (let x = 0; x < width; x += 1) {
      const left = x * scaleX;
      const right = (x + 1) * scaleX;
      sampleArea(source, output, (y * width + x) * 4, left, top, right, bottom);
    }
  }
  return { width, height, bitDepth: 8, colorType: 6, pixels: output };
}

export function recoverAlpha(blackMatte, whiteMatte) {
  if (blackMatte.width !== whiteMatte.width || blackMatte.height !== whiteMatte.height) {
    throw new Error("matte dimensions do not match");
  }
  const pixels = Buffer.alloc(blackMatte.width * blackMatte.height * 4);
  for (let offset = 0; offset < pixels.length; offset += 4) {
    const difference = Math.round((
      whiteMatte.pixels[offset] - blackMatte.pixels[offset]
      + whiteMatte.pixels[offset + 1] - blackMatte.pixels[offset + 1]
      + whiteMatte.pixels[offset + 2] - blackMatte.pixels[offset + 2]
    ) / 3);
    const alpha = Math.max(0, Math.min(255, 255 - difference));
    pixels[offset + 3] = alpha;
    if (alpha === 0) continue;
    pixels[offset] = Math.min(255, Math.round((blackMatte.pixels[offset] * 255) / alpha));
    pixels[offset + 1] = Math.min(255, Math.round((blackMatte.pixels[offset + 1] * 255) / alpha));
    pixels[offset + 2] = Math.min(255, Math.round((blackMatte.pixels[offset + 2] * 255) / alpha));
  }
  return { width: blackMatte.width, height: blackMatte.height, bitDepth: 8, colorType: 6, pixels };
}

function sampleArea(source, output, target, left, top, right, bottom) {
  let alphaWeight = 0;
  let totalWeight = 0;
  let red = 0;
  let green = 0;
  let blue = 0;
  for (let sy = Math.floor(top); sy < Math.ceil(bottom); sy += 1) {
    const yWeight = Math.min(bottom, sy + 1) - Math.max(top, sy);
    for (let sx = Math.floor(left); sx < Math.ceil(right); sx += 1) {
      const xWeight = Math.min(right, sx + 1) - Math.max(left, sx);
      const weight = xWeight * yWeight;
      const sourceOffset = (sy * source.width + sx) * 4;
      const alpha = source.pixels[sourceOffset + 3] / 255;
      const premultipliedWeight = weight * alpha;
      red += source.pixels[sourceOffset] * premultipliedWeight;
      green += source.pixels[sourceOffset + 1] * premultipliedWeight;
      blue += source.pixels[sourceOffset + 2] * premultipliedWeight;
      alphaWeight += premultipliedWeight;
      totalWeight += weight;
    }
  }
  if (alphaWeight > 0) {
    output[target] = Math.round(red / alphaWeight);
    output[target + 1] = Math.round(green / alphaWeight);
    output[target + 2] = Math.round(blue / alphaWeight);
  }
  output[target + 3] = Math.round((alphaWeight / totalWeight) * 255);
}

export function solidImage(width, height, red, green, blue, alpha = 255) {
  const pixels = Buffer.alloc(width * height * 4);
  for (let offset = 0; offset < pixels.length; offset += 4) {
    pixels[offset] = red;
    pixels[offset + 1] = green;
    pixels[offset + 2] = blue;
    pixels[offset + 3] = alpha;
  }
  return { width, height, bitDepth: 8, colorType: 6, pixels };
}

export function composite(target, source, left, top) {
  for (let y = 0; y < source.height; y += 1) {
    for (let x = 0; x < source.width; x += 1) {
      const targetOffset = ((top + y) * target.width + left + x) * 4;
      const sourceOffset = (y * source.width + x) * 4;
      blendPixel(target.pixels, targetOffset, source.pixels, sourceOffset);
    }
  }
}

function blendPixel(target, targetOffset, source, sourceOffset) {
  const sourceAlpha = source[sourceOffset + 3] / 255;
  const targetAlpha = target[targetOffset + 3] / 255;
  const outputAlpha = sourceAlpha + targetAlpha * (1 - sourceAlpha);
  if (outputAlpha === 0) return;
  for (let channel = 0; channel < 3; channel += 1) {
    target[targetOffset + channel] = Math.round(
      (source[sourceOffset + channel] * sourceAlpha
        + target[targetOffset + channel] * targetAlpha * (1 - sourceAlpha))
        / outputAlpha,
    );
  }
  target[targetOffset + 3] = Math.round(outputAlpha * 255);
}
