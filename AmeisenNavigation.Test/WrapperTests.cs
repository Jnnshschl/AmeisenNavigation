using System;
using System.Collections.Generic;
using AmeisenNavigation.Server.Objects;
using AmeisenNavigationWrapper;
using Microsoft.VisualStudio.TestTools.UnitTesting;

namespace AmeisenNavigation.Test
{
    [TestClass]
    public class WrapperTests
    {
        [TestMethod]
        public void PathfindingTest()
        {
            int pathSize;
            List<Vector3> path = new List<Vector3>();

            float[] start = { -8826.562500f, -371.839752f, 71.638428f };
            float[] end = { -8918.406250f, -130.297256f, 80.906364f };

            Settings settings = new Settings();
            AmeisenNav ameisenNav = new AmeisenNav(settings.MmapsFolder);

            unsafe
            {
                fixed (float* pStart = start)
                fixed (float* pEnd = end)
                {
                    float* path_raw = ameisenNav.GetPath(0, pStart, pEnd, &pathSize);

                    for (int i = 0; i < pathSize * 3; i += 3)
                    {
                        path.Add(new Vector3(path_raw[i], path_raw[i + 1], path_raw[i + 2]));
                    }
                }
            }

            Assert.IsTrue(path.Count == 45);
        }
    }
}
