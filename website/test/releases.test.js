import { test } from 'node:test';
import assert from 'node:assert/strict';
import { markdown, highlights, apkDownloads } from '../src/lib/releases.js';

test('release notes keep their markdown', () => {
  assert.equal(markdown('## New\n- **Big** `x` [site](https://a.b/c)'),
    '<h4>New</h4><ul><li><strong>Big</strong> <code>x</code> <a href="https://a.b/c" rel="noopener nofollow">site</a></li></ul>');
});

test("release notes can't inject HTML", () => {
  const html = markdown('<img src=x onerror=alert(1)>\n- [x](https://a"onmouseover="alert(1))\n- [y](javascript:alert(1))');
  assert.ok(!html.includes('<img'));
  assert.ok(!/href="[^"]*"on/.test(html));
  assert.ok(!html.includes('href="javascript'));
  assert.ok(!highlights('## New\n- <script>alert(1)</script>')[0].includes('<script'));
});

test('downloads count every APK, pre-releases too, never drafts or other files', () => {
  const apk = (n) => ({ name: 'PokeDaisy-1.0.apk', download_count: n });
  assert.equal(apkDownloads([
    { assets: [apk(10), { name: 'notes.txt', download_count: 99 }] },
    { prerelease: true, assets: [apk(5)] },
    { draft: true, assets: [apk(1000)] },
    { assets: [] },
  ]), 15);
});
