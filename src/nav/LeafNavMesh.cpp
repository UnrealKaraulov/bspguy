#include "Renderer.h"
#include "LeafNavMesh.h"
#include "PolyOctree.h"
#include "Clipper.h"
#include "log.h"
#include "util.h"
#include <string.h>
#include "Bsp.h"
#include <unordered_map>
#include "Entity.h"
#include "Fgd.h"
#include <queue>
#include <algorithm>
#include <limits.h>
#include "GLFW/glfw3.h"

LeafNode::LeafNode() {
	id = -1;
	entidx = 0;
	center = origin = mins = maxs = vec3();
}

bool LeafNode::isInside(vec3 p) {
	for (size_t i = 0; i < leafFaces.size(); i++) {
		if (leafFaces[i].distance(p) > 0) {
			return false;
		}
	}

	return true;
}

bool LeafNode::addLink(int node, Polygon3D linkArea) {	
	for (size_t i = 0; i < links.size(); i++) {
		if (links[i].node == node) {
			return true;
		}
	}

	LeafLink link;
	link.linkArea = linkArea;
	link.node = node;

	link.pos = linkArea.center;
	if (fabs(linkArea.plane_z.z) < 0.7f) {
		// wall links should be positioned at the bottom of the intersection to keep paths near the floor
		linkArea.intersect2D(linkArea.center, linkArea.center - vec3(0, 0, 4096), link.pos);
		link.pos.z += NAV_BOTTOM_EPSILON;
	}

	links.push_back(link);

	return true;
}

bool LeafNode::addLink(int node, vec3 linkPos) {
	for (size_t i = 0; i < links.size(); i++) {
		if (links[i].node == node) {
			return true;
		}
	}

	LeafLink link;
	link.node = node;
	link.pos = linkPos;
	links.push_back(link);

	return true;
}

LeafNavMesh::LeafNavMesh() {
	clear();
	octree = NULL;
}

void LeafNavMesh::clear() {
	memset(leafMap, 65535, sizeof(unsigned int) * MAX_MAP_CLIPNODE_LEAVES);
	nodes.clear();
}

LeafNavMesh::LeafNavMesh(std::vector<LeafNode> inleaves, LeafOctree* octree) {
	clear();

	this->nodes = std::move(inleaves);
	this->octree = octree;
}

bool LeafNavMesh::addLink(int from, int to, Polygon3D linkArea) {
	if (from < 0 || to < 0 || from >= (int)nodes.size() || to >= (int)nodes.size()) {
		print_log("Error: add link from/to invalid node {} {}\n", from, to);
		return false;
	}

	if (!nodes[from].addLink(to, linkArea)) {
		vec3& pos = nodes[from].center;
		print_log("Failed to add link at {} {} {}\n", (int)pos.x, (int)pos.y, (int)pos.z);
		return false;
	}

	return true;
}

int LeafNavMesh::getNodeIdx(Bsp* map, Entity* ent) {
	vec3 ori = ent->origin;
	vec3 mins, maxs;
	int modelIdx = ent->getBspModelIdx();

	if (modelIdx != -1) {
		map->get_model_vertex_bounds(modelIdx, mins, maxs);
		ori += (maxs + mins) * 0.5f;
	}
	else {
		FgdClass* fclass = g_app->fgd->getFgdClass(ent->keyvalues["classname"]);
		if (fclass->sizeSet) {
			mins = fclass->mins;
			maxs = fclass->maxs;
		}
	}

	// first try testing a few points on the entity box for an early exit

	mins += ori;
	maxs += ori;

	vec3 testPoints[10] = {
		ori,
		(mins + maxs) * 0.5f,
		vec3(mins.x, mins.y, mins.z),
		vec3(mins.x, mins.y, maxs.z),
		vec3(mins.x, maxs.y, mins.z),
		vec3(mins.x, maxs.y, maxs.z),
		vec3(maxs.x, mins.y, mins.z),
		vec3(maxs.x, mins.y, maxs.z),
		vec3(maxs.x, maxs.y, mins.z),
		vec3(maxs.x, maxs.y, maxs.z),
	};

	for (int i = 0; i < 10; i++) {
		int targetLeaf = map->get_leaf(testPoints[i], 3);

		if (targetLeaf >= 0 && targetLeaf < MAX_MAP_CLIPNODE_LEAVES) {
			int navIdx = leafMap[targetLeaf];
			
			if (navIdx < 65535) {
				return navIdx;
			}
		}
	}

	if ((maxs - mins).length() < 1) {
		return -1; // point sized, so can't intersect any leaf
	}

	// no points are inside, so test for plane intersections

	cCube entCube(mins, maxs, COLOR4(0, 0, 0, 0));
	cQuad* faces[6] = {
		&entCube.top,
		&entCube.bottom,
		&entCube.left,
		&entCube.right,
		&entCube.front,
		&entCube.back,
	};

	Polygon3D boxPolys[6];
	for (int i = 0; i < 6; i++) {
		cQuad& face = *faces[i];
		boxPolys[i] = std::vector<vec3>{ face.v1.pos, face.v2.pos, face.v3.pos, face.v6.pos };
	}

	for (int i = 0; i < (int)nodes.size(); i++) {
		LeafNode& mesh = nodes[i];
		
		for (size_t k = 0; k < mesh.leafFaces.size(); k++) {
			Polygon3D& leafFace = mesh.leafFaces[k];

			for (int n = 0; n < 6; n++) {
				if (leafFace.intersects(boxPolys[n])) {
					return i;
				}
			}
		}
	}

	return -1;
}

float LeafNavMesh::path_cost(int a, int b) {

	LeafNode& nodea = nodes[a];
	LeafNode& nodeb = nodes[b];
	vec3 delta = nodea.origin - nodeb.origin;

	for (size_t i = 0; i < nodea.links.size(); i++) {
		LeafLink& link = nodea.links[i];
		if (link.node == b) {
			return link.baseCost + delta.length() * link.costMultiplier;
		}
	}

	return delta.length();
}

std::vector<int> LeafNavMesh::AStarRoute(int startNodeIdx, int endNodeIdx)
{
	std::set<int> closedSet;
	std::set<int> openSet;

	std::unordered_map<int, float> gScore;
	std::unordered_map<int, float> fScore;
	std::unordered_map<int, int> cameFrom;
	
	std::vector<int> emptyRoute;

	if (startNodeIdx < 0 || endNodeIdx < 0 || startNodeIdx > (int)nodes.size() || endNodeIdx > (int)nodes.size()) {
		print_log("AStarRoute: invalid start/end nodes\n");
		return emptyRoute;
	}

	if (startNodeIdx == endNodeIdx) {
		emptyRoute.push_back(startNodeIdx);
		return emptyRoute;
	}

	LeafNode& start = nodes[startNodeIdx];
	LeafNode& goal = nodes[endNodeIdx];

	openSet.insert(startNodeIdx);
	gScore[startNodeIdx] = 0;
	fScore[startNodeIdx] = path_cost(start.id, goal.id);

	const int maxIter = 8192;
	int curIter = 0;
	while (!openSet.empty()) {
		if (++curIter > maxIter) {
			print_log("AStarRoute exceeded max iterations searching path ({})", maxIter);
			break;
		}

		// get node in openset with lowest cost
		int current = -1;
		float bestScore = (float)9e99;
		for (int nodeId : openSet)
		{
			float score = fScore[nodeId];
			if (score < bestScore) {
				bestScore = score;
				current = nodeId;
			}
		}

		//println("Current is " + current);

		if (current == goal.id) {
			//println("MAde it to the goal");
			// goal reached, build the route
			std::vector<int> path;
			path.push_back(current);

			int maxPathLen = 1000;
			int i = 0;
			while (cameFrom.count(current)) {
				current = cameFrom[current];
				path.push_back(current);
				if (++i > maxPathLen) {
					print_log("AStarRoute exceeded max path length ({})", maxPathLen);
					break;
				}
			}
			reverse(path.begin(), path.end());

			return path;
		}

		openSet.erase(current);
		closedSet.insert(current);

		LeafNode& currentNode = nodes[current];

		for (size_t i = 0; i < currentNode.links.size(); i++) {
			LeafLink& link = currentNode.links[i];
			if (link.node == -1) {
				break;
			}

			int neighbor = link.node;
			if (neighbor < 0 || neighbor >= (int)nodes.size()) {
				continue;
			}
			if (closedSet.count(neighbor))
				continue;
			//if (currentNode.blockers.size() > i and currentNode.blockers[i] & blockers != 0)
			//	continue; // blocked by something (monsterclip, normal clip, etc.). Don't route through this path.

			// discover a new node
			openSet.insert(neighbor);

			// The distance from start to a neighbor
			LeafNode& neighborNode = nodes[neighbor];

			float tentative_gScore = gScore[current];
			tentative_gScore += path_cost(currentNode.id, neighborNode.id);

			float neighbor_gScore = (float)9e99;
			if (gScore.count(neighbor))
				neighbor_gScore = gScore[neighbor];

			if (tentative_gScore >= neighbor_gScore)
				continue; // not a better path

			// This path is the best until now. Record it!
			cameFrom[neighbor] = current;
			gScore[neighbor] = tentative_gScore;
			fScore[neighbor] = tentative_gScore + path_cost(neighborNode.id, goal.id);
		}
	}	
	
	return emptyRoute;
}

// Dijkstra's algorithm to find shortest path from start to end vertex (chat-gpt code)
std::vector<int> LeafNavMesh::dijkstraRoute(int start, int end) {
	std::vector<int> emptyRoute;

	if (start < 0 || end < 0 || start > (int)nodes.size() || end > (int)nodes.size()) {
		print_log("dijkstraRoute: invalid start/end nodes\n");
		return emptyRoute;
	}

	if (start == end) {
		emptyRoute.push_back(start);
		return emptyRoute;
	}

	size_t n = nodes.size();
	std::vector<float> dist(n, FLT_MAX); // Initialize distances with infinity
	std::vector<int> previous(n, -1); // Array to store previous node in the shortest path
	dist[start] = 0.0f; // Distance from start node to itself is 0

	std::priority_queue<std::pair<float, int>, std::vector<std::pair<float, int>>, std::greater<std::pair<float, int>>> pq;
	pq.push({ 0.0f, start }); // Push start node with distance 0

	while (!pq.empty()) {
		int u = pq.top().second; // Get node with smallest distance
		float d = pq.top().first; // Get the distance
		pq.pop();

		// If the extracted node is already processed, skip
		if (d > dist[u])
			continue;

		// Stop early if we reached the end node
		if (u == end)
			break;

		// Traverse all links of node u
		for (size_t i = 0; i < nodes[u].links.size(); i++) {
			LeafLink& link = nodes[u].links[i];

			if (link.node == -1) {
				break;
			}

			int v = link.node;
			float weight = path_cost(u, link.node);

			// Relaxation step
			if (dist[u] + weight < dist[v]) {
				dist[v] = dist[u] + weight;
				previous[v] = u; // Set previous node for path reconstruction
				pq.push({ dist[v], v });
			}
		}
	}

	// Reconstruct the shortest path from start to end node
	std::vector<int> path;
	for (int at = end; at != -1; at = previous[at]) {
		path.push_back(at);
		if (at == start)
			break;
	}
	reverse(path.begin(), path.end());

	// If end node is unreachable, return an empty path
	if (path.empty() || path[0] != start)
		return {};

	float len = 0;
	float cost = 0;
	for (size_t i = 1; i < path.size(); i++) {
		LeafNode& mesha = nodes[path[i-1]];
		LeafNode& meshb = nodes[path[i]];
		len += (mesha.origin - meshb.origin).length();
		cost += path_cost(path[i - 1], path[i]);
	}
	print_log("Path length: {}, cost: {}\n", (int)len, (int)cost);

	return path;
}