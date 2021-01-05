namespace AmeisenNavigation.Server.Transformations
{
    public static class ChaikinCurve
    {
        public static float[] Perform(float[] path, int iterations)
        {
            int orgSize = path.Length;
            int newSize = (path.Length * 2) - 3;
            float[] smoothPath = new float[newSize];

            for (int i = 0; i < orgSize - 3; i += 3)
            {
                int index = i * 2;

                // Q.X
                smoothPath[index] = 0.75f * path[i] + 0.25f * path[i + 3];
                // Q.Y
                smoothPath[index + 1] = 0.75f * path[i + 1] + 0.25f * path[i + 4];
                // Q.Z
                smoothPath[index + 2] = path[i + 2];

                // R.X
                smoothPath[index + 3] = 0.25f * path[i] + 0.75f * path[i + 3];
                // R.Y
                smoothPath[index + 4] = 0.25f * path[i + 1] + 0.75f * path[i + 4];
                // R.Z
                smoothPath[index + 5] = path[i + 5];
            }

            // last node
            smoothPath[newSize - 1] = path[orgSize - 1];
            smoothPath[newSize - 2] = path[orgSize - 2];
            smoothPath[newSize - 3] = path[orgSize - 3];

            if (iterations > 1)
            {
                return Perform(smoothPath, --iterations);
            }
            else
            {
                return smoothPath;
            }
        }
    }
}