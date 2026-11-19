"use client";

import * as React from "react";
import { Card, CardContent, CardHeader, CardTitle, CardDescription } from "@/components/ui/card";
import { Button } from "@/components/ui/button";
import { Separator } from "@/components/ui/separator";
import { Plus, Image, Edit, Upload, Loader2, Eye } from "lucide-react";

export function FloorPlans() {
  return (
    <div className="space-y-6">
      <div className="flex flex-col sm:flex-row sm:items-center sm:justify-between gap-4">
        <div>
          <h1 className="text-3xl font-bold tracking-tight">Floor Plans</h1>
          <p className="text-muted-foreground">Upload and manage floor plan SVGs for visual control</p>
        </div>
        <Button variant="outline" disabled>
          <Upload className="mr-2 h-4 w-4" />
          Upload SVG
        </Button>
      </div>

      <Card>
        <CardHeader>
          <CardTitle>Coming in Phase 8</CardTitle>
          <CardDescription>
            Floor plan editor with SVG sanitization, control mode (inline SVG + Motion glows),
            and edit mode (React Konva) will be implemented in Phase 8.
          </CardDescription>
        </CardHeader>
        <CardContent className="space-y-4">
          <div className="grid gap-4 md:grid-cols-3">
            <FeatureCard
              title="SVG Upload & Sanitization"
              description="Upload floor plan SVGs with automatic sanitization for security"
              icon={<Image />}
            />
            <FeatureCard
              title="Control Mode"
              description="Inline SVG with Motion glows driven by live device state"
              icon={<Eye />}
            />
            <FeatureCard
              title="Edit Mode"
              description="React Konva placement editor for light positions"
              icon={<Edit />}
            />
          </div>

          <Separator />

          <h3 className="text-lg font-medium">Features</h3>
          <ul className="space-y-2 text-sm text-muted-foreground">
            <li>• SVG sanitization on upload and render</li>
            <li>• ViewBox coordinate math for responsive scaling</li>
            <li>• Control mode: inline SVG + Motion glows</li>
            <li>• Edit mode: React Konva placement editor</li>
            <li>• Room regions with mode switching</li>
            <li>• Malicious SVG test suite</li>
            <li>• Shared viewBox coordinates between renderers</li>
          </ul>

          <div className="text-center py-4">
            <Button variant="outline" disabled>
              <Loader2 className="mr-2 h-4 w-4 animate-spin" />
              Phase 8 Implementation Pending
            </Button>
          </div>
        </CardContent>
      </Card>
    </div>
  );
}

function FeatureCard({ title, description, icon }: { title: string; description: string; icon: React.ReactNode }) {
  return (
    <Card className="h-full">
      <CardContent className="pt-6">
        <div className="flex h-12 w-12 items-center justify-center rounded-lg bg-primary/10 mb-4">
          {icon}
        </div>
        <h4 className="font-medium">{title}</h4>
        <p className="text-sm text-muted-foreground mt-1">{description}</p>
      </CardContent>
    </Card>
  );
}