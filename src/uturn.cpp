//
// Copyright [2022] Manchester Metropolitian University"  [legal/copyright]
//
#include <algorithm>
#include <iostream>
#include <list>
#include <string>
#include <vector>

#include "full_coverage_path_planner/uturn.h"
#include <pluginlib/class_list_macros.h>

// register this planner as a BaseGlobalPlanner plugin
PLUGINLIB_EXPORT_CLASS(full_coverage_path_planner::UTurn, nav_core::BaseGlobalPlanner)

namespace full_coverage_path_planner
{
void UTurn::initialize(std::string name, costmap_2d::Costmap2DROS* costmap_ros)
{
  if (!initialized_)
  {
    // Create a publisher to visualize the plan
    ros::NodeHandle private_nh("~/");
    ros::NodeHandle nh, private_named_nh("~/" + name);

    plan_pub_ = private_named_nh.advertise<nav_msgs::Path>("plan", 1);
    // Try to request the cpp-grid from the cpp_grid map_server
    cpp_grid_client_ = nh.serviceClient<nav_msgs::GetMap>("static_map");

    // Define  robot radius (radius) parameter
    float robot_radius_default = 0.5f;
    private_named_nh.param<float>("robot_radius", robot_radius_, robot_radius_default);
    // Define  tool radius (radius) parameter
    float tool_radius_default = 0.5f;
    private_named_nh.param<float>("tool_radius", tool_radius_, tool_radius_default);
    initialized_ = true;
  }
}

std::list<gridNode_t> UTurn::move(std::vector<std::vector<bool> > const& grid, std::list<gridNode_t>& init,
                                        std::vector<std::vector<bool> >& visited)
{
  int dx, dy, dx_prev, x2, y2, i, nRows = grid.size(), nCols = grid[0].size();
  // Spiral filling of the open space
  // Copy incoming list to 'end'
  std::list<gridNode_t> pathNodes(init);
  // Create iterator for gridNode_t list and let it point to the last element of end
  std::list<gridNode_t>::iterator it = --(pathNodes.end());
  if (pathNodes.size() > 1)  // if list is length 1, keep iterator at end
    it--;                    // Let iterator point to second to last element

  gridNode_t prev = *(it);
  bool done = false;
  while (!done)
  {
    dx = 0;
    dy = 1;
    done = true;
    
    for (int i = 0; i < 4; ++i)
    {
      x2 = pathNodes.back().pos.x + dx;
      y2 = pathNodes.back().pos.y + dy;
      if (x2 >= 0 && x2 < nCols && y2 >= 0 && y2 < nRows)
      {
        if (grid[y2][x2] == eNodeOpen && visited[y2][x2] == eNodeOpen)
        {
          Point_t new_point = { x2, y2 };
          gridNode_t new_node =
          {
            new_point,  // Point: x,y
            0,          // Cost
            0,          // Heuristic
          };
          prev = pathNodes.back();
          pathNodes.push_back(new_node);
          it = --(pathNodes.end());
          visited[y2][x2] = eNodeVisited;  // Close node
          done = false;
          break;
        }
      }
      // try next direction cw
      dx_prev = dx;
      dx = dy;
      dy = -dx_prev;
    }
  }
  return pathNodes;
}

std::list<Point_t> UTurn::moveFull(std::vector<std::vector<bool> > const& grid,
                                          Point_t& init,
                                          int &multiple_pass_counter,
                                          int &visited_counter)
{
  int x, y, nRows = grid.size(), nCols = grid[0].size();
  // Initial node is initially set as visited so it does not count
  multiple_pass_counter = 0;
  visited_counter = 0;

  std::vector<std::vector<bool> > visited;
  visited = grid;  // Copy grid matrix
  x = init.x;
  y = init.y;

  Point_t new_point = { x, y };
  gridNode_t new_node =
  {
    new_point,  // Point: x,y
    0,          // Cost
    0,          // Heuristic
  };
  std::list<gridNode_t> pathNodes;
  std::list<Point_t> fullPath;
  pathNodes.push_back(new_node);
  visited[y][x] = eNodeVisited;

#ifdef DEBUG_PLOT
  ROS_INFO("Grid before walking is: ");
  printGrid(grid, visited, fullPath);
#endif

  std::list<Point_t> goals = map_2_goals(visited, eNodeOpen);  // Retrieve remaining goalpoints
  // Add points to full path
  std::list<gridNode_t>::iterator it;
  for (it = pathNodes.begin(); it != pathNodes.end(); ++it)
  {
    Point_t newPoint = { it->pos.x, it->pos.y };
    visited_counter++;
    fullPath.push_back(newPoint);
  }

#ifdef DEBUG_PLOT
  ROS_INFO("Current grid after first spiral is");
  printGrid(grid, visited, fullPath);
  ROS_INFO("There are %d goals remaining", goals.size());
#endif
  while (goals.size() != 0)
  {
    // Remove all elements from pathNodes list except last element.
    // The last point is the starting point for a new search and A* extends the path from there on
    pathNodes.erase(pathNodes.begin(), --(pathNodes.end()));
    visited_counter--;  // First point is already counted as visited
    // Plan to closest open Node using A*
    // `goals` is essentially the map, so we use `goals` to determine the distance from the end of a potential path
    //    to the nearest free space
    bool resign = a_star_to_open_space(grid, pathNodes.back(), 1, visited, goals, pathNodes);
    if (resign)
    {
#ifdef DEBUG_PLOT
      ROS_INFO("A_star_to_open_space is resigning", goals.size());
#endif
      break;
    }

    // Update visited grid
    for (it = pathNodes.begin(); it != pathNodes.end(); ++it)
    {
      if (visited[it->pos.y][it->pos.x])
      {
        multiple_pass_counter++;
      }
      visited[it->pos.y][it->pos.x] = eNodeVisited;
    }
    if (pathNodes.size() > 0)
    {
      multiple_pass_counter--;  // First point is already counted as visited
    }

#ifdef DEBUG_PLOT
    ROS_INFO("Grid with path marked as visited is:");
    gridNode_t SpiralStart = pathNodes.back();
    printGrid(grid, visited, pathNodes, pathNodes.front(), pathNodes.back());
#endif

    // Spiral fill from current position
    pathNodes = move(grid, pathNodes, visited);

#ifdef DEBUG_PLOT
    ROS_INFO("Visited grid updated after spiral:");
    printGrid(grid, visited, pathNodes, SpiralStart, pathNodes.back());
#endif

    goals = map_2_goals(visited, eNodeOpen);  // Retrieve remaining goalpoints

    for (it = pathNodes.begin(); it != pathNodes.end(); ++it)
    {
      Point_t newPoint = { it->pos.x, it->pos.y };
      visited_counter++;
      fullPath.push_back(newPoint);
    }
  }

  return fullPath;
}

bool UTurn::makePlan(const geometry_msgs::PoseStamped& start, const geometry_msgs::PoseStamped& goal,
                         std::vector<geometry_msgs::PoseStamped>& plan)
{
  if (!initialized_)
  {
    ROS_ERROR("This planner has not been initialized yet, but it is being used, please call initialize() before use");
    return false;
  }
  else
  {
    ROS_INFO("Initialized!");
  }

  clock_t begin = clock();
  Point_t startPoint;

  /********************** Get grid from server **********************/
  std::vector<std::vector<bool> > grid;
  nav_msgs::GetMap grid_req_srv;
  ROS_INFO("Requesting grid!!");
  if (!cpp_grid_client_.call(grid_req_srv))
  {
    ROS_ERROR("Could not retrieve grid from map_server");
    return false;
  }

  if (!parseGrid(grid_req_srv.response.map, grid, robot_radius_ * 2, tool_radius_ * 2, start, startPoint))
  {
    ROS_ERROR("Could not parse retrieved grid");
    return false;
  }

#ifdef DEBUG_PLOT
  ROS_INFO("Start grid is:");
  std::list<Point_t> printPath;
  printPath.push_back(startPoint);
  printGrid(grid, grid, printPath);
#endif

  std::list<Point_t> goalPoints = moveFull(grid,
                                              startPoint,
                                              spiral_cpp_metrics_.multiple_pass_counter,
                                              spiral_cpp_metrics_.visited_counter);
  ROS_INFO("naive cpp completed!");
  ROS_INFO("Converting path to plan");


  clock_t end = clock();
  float elapsed_secs = static_cast<float>((end - begin) / CLOCKS_PER_SEC)*1000;

  parsePointlist2Plan(start, goalPoints, plan);
  // Print some metrics:
  spiral_cpp_metrics_.multiple_pass_counter = 0;
  spiral_cpp_metrics_.visited_counter = 0;
  auto test_grid(grid);
  for (Point_t nodeOnPath : goalPoints)
  {
      spiral_cpp_metrics_.visited_counter++;
      if(test_grid[nodeOnPath.y][nodeOnPath.x] == eNodeVisited)
      {
          spiral_cpp_metrics_.multiple_pass_counter++;
      }
      else
      {
          test_grid[nodeOnPath.y][nodeOnPath.x] = eNodeVisited;

      }
  }
  ROS_INFO("Total visited: %d", spiral_cpp_metrics_.visited_counter);
  ROS_INFO("Total re-visited: %d", spiral_cpp_metrics_.multiple_pass_counter);

  // TODO(CesarLopez): Check if global path should be calculated repetitively or just kept
  // (also controlled by planner_frequency parameter in move_base namespace)

  ROS_INFO("Publishing plan!");
  publishPlan(plan);
  ROS_INFO("Plan published!");
  ROS_DEBUG("Plan published");

  std::cout << "elapsed time: " << elapsed_secs << "\n";

  return true;
}
}  // namespace full_coverage_path_planner
