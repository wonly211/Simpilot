import fs from 'node:fs/promises';
import path from 'node:path';
import crypto from 'node:crypto';

// Archive original screenshot payloads for the deliverable and explicit reviewer attachments.
// This is not a replacement screenshot or input mechanism.
export async function archiveState(state, id, details, root) {
  if (!/^[A-Z]\d{2,3}$/.test(id)) throw new Error('Use a stable evidence ID');
  await fs.mkdir(root, { recursive: true });
  const indexPath = path.join(root, 'index.json');
  let index = [];
  try { index = JSON.parse(await fs.readFile(indexPath, 'utf8')); }
  catch (error) { if (error.code !== 'ENOENT') throw error; }
  if (index.some(item => item.id === id)) throw new Error(`Duplicate evidence: ${id}`);
  const images = [];
  for (const [offset, image] of state.screenshots.entries()) {
    const match = /^data:image\/(png|jpeg);base64,(.*)$/s.exec(image.url);
    if (!match) throw new Error('Unsupported original screenshot format');
    const bytes = Buffer.from(match[2], 'base64');
    const file = `${id}${offset ? `-${offset}` : ''}.${match[1] === 'jpeg' ? 'jpg' : 'png'}`;
    await fs.writeFile(path.join(root, file), bytes);
    images.push({
      file, sha256: crypto.createHash('sha256').update(bytes).digest('hex'),
      width: image.width, height: image.height, originX: image.originX,
      originY: image.originY, zIndex: image.zIndex,
    });
  }
  if (!images.length) throw new Error('No screenshot evidence');
  await fs.writeFile(path.join(root, `${id}.txt`), state.accessibility?.tree ?? '', 'utf8');
  const record = {
    id, capturedAt: new Date().toISOString(), title: state.window.title,
    ...details, images,
  };
  index.push(record);
  await fs.writeFile(indexPath, JSON.stringify(index, null, 2) + '\n', 'utf8');
  return record;
}
