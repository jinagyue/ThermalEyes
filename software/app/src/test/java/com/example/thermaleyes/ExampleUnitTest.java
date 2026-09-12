package com.example.thermaleyes;

import org.junit.Test;

import static org.junit.Assert.*;

/**
 * Example local unit test, which will execute on the development machine (host).
 *
 * @see <a href="http://d.android.com/tools/testing">Testing documentation</a>
 */
public class ExampleUnitTest {
    @Test
    public void addition_isCorrect() {
        assertEquals(4, 2 + 2);
    }

    @Test
    public void testAffineMappingCenter() {
        int thermW = 240, thermH = 240;
        int camW = 640, camH = 480;
        int offsetX = 10, offsetY = 25;
        float scale = 1.2f;
        float rotation = 0f;

        // Center point
        float x0 = 120 * ((float) camW / thermW);
        float y0 = 120 * ((float) camH / thermH);
        float cx = camW / 2.0f;
        float cy = camH / 2.0f;
        double rad = Math.toRadians(rotation);
        double cos = Math.cos(rad);
        double sin = Math.sin(rad);
        double alpha = scale * cos;
        double beta = scale * sin;
        double dx = x0 - cx;
        double dy = y0 - cy;

        int xAligned = (int) Math.round(cx + alpha * dx + beta * dy + offsetX);
        int yAligned = (int) Math.round(cy - beta * dx + alpha * dy + offsetY);

        assertEquals(320 + 10, xAligned);
        assertEquals(240 + 25, yAligned);
    }
}