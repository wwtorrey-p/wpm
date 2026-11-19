import { test } from 'node:test';
import assert from 'node:assert/strict';
import { productionAverage, productionHistory } from './production.js';

test('completed windows include zero days and exclude today', () => {
  const history = productionHistory([{ day: 1, gallons: 210 }, { day: 29, gallons: 30 }, { day: 31, gallons: 100 }]);
  assert.deepEqual(productionAverage(history, 30, 30), { gallons: 240, days: 30, average: 8 });
  assert.deepEqual(productionAverage(history, 31, 30), { gallons: 130, days: 30, average: 130 / 30 });
});

test('reports partial coverage since first record and unavailable earlier periods', () => {
  const history = productionHistory([{ day: 20, gallons: 100 }, { day: 22, gallons: 50 }]);
  assert.deepEqual(productionAverage(history, 22, 7), { gallons: 150, days: 3, average: 50 });
  assert.equal(productionAverage(history, 19, 30), null);
  assert.equal(productionAverage(productionHistory([]), 22, 7), null);
});

test('supplied history explains the 30-day drop', () => {
  const history = productionHistory([
    { day: 20746, gallons: 222.9124 }, { day: 20748, gallons: 126.4182 },
    { day: 20750, gallons: 13.4167 }, { day: 20759, gallons: 1.1667 },
    { day: 20760, gallons: 58.3330 }, { day: 20761, gallons: 52.5831 },
    { day: 20762, gallons: 113.2511 }, { day: 20768, gallons: 87.4170 },
    ...[20773, 20774, 20775, 20776].map((day) => ({ day, gallons: 101.0007 })),
  ]);
  assert.equal(productionAverage(history, 20775, 30).average.toFixed(1), '32.6');
  assert.equal(productionAverage(history, 20776, 30).average.toFixed(1), '28.6');
  assert.equal(productionAverage(history, 20775, 7).average.toFixed(1), '43.3');
});
