// Passive UI state only. USB/status never controls the realtime video path.
export class ReceiverConsole {
  constructor() { this.reset(); }

  reset() {
    this.pending = '';
    this.version = '';
    this.current = false;
    this.directMenu = false;
    this.control = {};
    this.output = {};
  }

  push(chunk) {
    const lines = (this.pending + chunk).split('\n');
    this.pending = lines.pop().slice(-4096);
    for (const raw of lines) {
      const line = raw.replace(/\r/g, '');
      const version = line.match(/^C5VRX5 (?:version=(\S+)|(\S+) ready:)/);
      if (version) {
        this.current = true;
        this.version = version[1] || version[2];
      }
      if (line.startsWith('C5VRX-5 keys:')) this.current = true;
      if (/^\s+v video standard AUTO\/NTSC\/PAL, e close menu/.test(line)) this.directMenu = true;
      for (const [prefix, target] of [['CONTROL ', this.control], ['OUTPUT ', this.output]]) {
        if (!this.current || !line.startsWith(prefix)) continue;
        for (const match of line.matchAll(/(?:^|\s)(\w+)=([^\s]+)/g)) target[match[1]] = match[2];
      }
    }
  }

  supports(key) {
    return ['T', 'h', '?'].includes(key) || (this.current && (!['v', 'e'].includes(key) || this.directMenu));
  }
}
