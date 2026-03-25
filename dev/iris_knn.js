// iris_knn.js
autowatch = 1;
outlets = 3; // 0: ids, 1: dists, 2: lcd_drawing

post("IRIS KNN Loaded\n");

var points = [];   // {id:int, x:float, y:float}
var locked = 0;

// Internal state for listener position (for redrawing)
var last_listen_x = -1;
var last_listen_y = -1;

function lock(v) {
  locked = (v !== 0) ? 1 : 0;
  // When unlocking, we might want to clear the listener or just leave it.
  // Let's ensure we redraw the static points at least.
  draw();
}

function clear() {
  points = [];
  last_listen_x = -1;
  last_listen_y = -1;
  outlet(0, "nearest");
  outlet(1, "dist");
  draw();
}

function add(id, x, y) {
  if (locked) return; // Only allow adding when unlocked

  // If id already exists, overwrite
  id = parseInt(id);
  x = parseFloat(x);
  y = parseFloat(y);

  var found = false;
  for (var i = 0; i < points.length; i++) {
    if (points[i].id === id) {
      points[i].x = x;
      points[i].y = y;
      found = true;
      break;
    }
  }
  if (!found) {
    points.push({ id: id, x: x, y: y });
  }
  draw();
}

function listen(x, y) {
  if (!locked) return; // Only listen/calculate when locked

  x = parseFloat(x);
  y = parseFloat(y);
  last_listen_x = x;
  last_listen_y = y;

  if (points.length === 0) {
    outlet(0, "nearest");
    outlet(1, "dist");
    draw();
    return;
  }

  // Compute distances
  var dists = [];
  for (var i = 0; i < points.length; i++) {
    var dx = points[i].x - x;
    var dy = points[i].y - y;
    var d = Math.sqrt(dx * dx + dy * dy);
    dists.push({ id: points[i].id, d: d, x: points[i].x, y: points[i].y });
  }

  // Sort by distance
  dists.sort(function (a, b) { return a.d - b.d; });

  // Take up to 3
  var k = Math.min(3, dists.length);
  var ids = ["nearest"];
  var ds = ["dist"];

  // We will pass the top k neighbors to the draw function
  var neighbors = [];

  for (var j = 0; j < k; j++) {
    ids.push(dists[j].id);
    ds.push(dists[j].d);
    neighbors.push(dists[j]);
  }

  outlet(0, ids);
  outlet(1, ds);

  draw(neighbors);
}

function draw(neighbors) {
  outlet(2, "clear");

  // existing points are drawn in blue (unlocked) or black (locked)
  // or maybe just always black dots
  for (var i = 0; i < points.length; i++) {
    outlet(2, "frgb", 0, 0, 0); // black
    outlet(2, "paintoval", points[i].x - 3, points[i].y - 3, points[i].x + 3, points[i].y + 3);
    // Draw ID text? optional
    // outlet(2, "moveto", points[i].x + 5, points[i].y);
    // outlet(2, "text", points[i].id);
  }

  // If we are placing points (unlocked), maybe indicate that? 
  // For now, simple is fine.

  // If we have a listener position and we are locked
  if (locked && last_listen_x >= 0 && last_listen_y >= 0) {
    // Draw listener in Red
    outlet(2, "frgb", 255, 0, 0);
    outlet(2, "paintoval", last_listen_x - 5, last_listen_y - 5, last_listen_x + 5, last_listen_y + 5);

    // Draw lines to nearest neighbors
    if (neighbors) {
      outlet(2, "frgb", 200, 0, 0); // lighter red lines
      for (var j = 0; j < neighbors.length; j++) {
        outlet(2, "linesegment", last_listen_x, last_listen_y, neighbors[j].x, neighbors[j].y);
      }
    }
  }
}
