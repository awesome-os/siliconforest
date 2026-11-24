# SPDX-License-Identifier: MIT
# Finch's own sources only; no Finch build/, SDK or Apple bundle enters a recipe.
builtins.fetchGit {
  url = "file:///home/chrisk/src/darwin-lab/sources/finch";
  shallow = true;
  rev = "2d9b1bb1af57e14462e4621d194929188a8845b0";
}
